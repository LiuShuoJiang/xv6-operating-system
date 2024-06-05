# Lecture 3: OS design

Lecture Topic:

- OS design -- high level
- Starting up xv6

OS Picture:

- apps: sh, echo, ...
- system call interface (open, read, fork, ...)
- kernel
- CPU, RAM, disk
- "OS" versus "kernel"

## Isolation

Isolation a big reason for separate protected kernel

### Difference Kinds of Isolation

#### Strawman design

**Strawman design**: No OS

- [sh, echo | CPU, RAM, disk]
- Applications directly interact with hardware
- efficient! flexible! and sometimes a good idea.

Main problem with No OS: lack of isolation

- **Resource isolation**:
  - One app uses too much memory, or hogs the CPU, or uses all the disk space
- **Memory isolation**:
  - One app's bug writes into another's memory

#### UNIX System Calls

Unix system call interface limits apps in a way that helps isolation

- often by abstracting hardware resources
- `fork()` and processes **abstract cores**
  - OS transparently switches cores among processes
  - ***Enforces*** that processes give them up
  - Can have more processes than cores
- `exec() / sbrk()` and virtual addresses **abstract RAM**
  - Each process has its "own" memory -- an ***address space***
  - OS decides where to place app in memory
  - OS confines a process to using its own memory
- files **abstract disk-level blocks**
  - OS ensures that different uses don't conflict
  - OS enforces permissions
- pipes **abstract memory sharing**

System call interface carefully thought out to provide isolation, but still allow controlled sharing, and portability

### Security

Isolation is about security as well as bugs

What do OS designers assume about security? (defensive)

- We assume user code is actively malicious
  - Actively trying to break out of isolation
  - Actively trying to trick system calls into doing something stupid
  - Actively trying to interfere with other programs
- We assume kernel code is trustworthy
  - We assume kernel developers are well-meaning and competent
  - We're not too worried about kernel code abusing other kernel code.
  - Of course, there are nevertheless bugs in kernels
- So kernel must treat all user interaction carefully => Requires a security mindset
  - Any bug in kernel may be a security exploit

### Different Kinds of Isolation

How can a kernel defend itself against user code?

two big components:

- hardware-level controls on user instructions
- careful system call interface and implementation

#### Hardware-Level Isolation

CPUs and kernels are co-designed

- **user/supervisor mode**
- **virtual memory**

user/supervisor mode (also called kernel mode)

- **supervisor mode**: can execute "privileged" instructions
  - e.g., device h/w access
  - e.g., modify page tables
- **user mode**: cannot execute privileged instructions
- ***Kernel*** in supervisor mode, ***applications*** in user mode
- [RISC-V has also an **M mode** (Machine mode), which we mostly ignore]

Processors provide virtual memory

- **page table** maps Virtual Addresses -> Physical Addresses
- Limits what memory a user process can use
- OS sets up page tables so that each application can access only its memory
  - And cannot get at kernel's memory
- Page table only be changed in supervisor mode
- We'll spend a lot of time looking at virtual memory...

The RISC-V Instruction Set Manual Volume II: Privileged Architecture

- supervisor-only instructions, registers -- p. 11
- page tables

#### System Calls

How do system calls work?

- Applications run in user mode
- System calls must execute in kernel in supervisor mode
- Must somehow allow applications to get at privileged resources!

Solution: instruction to change mode in controlled way

- `open()`:
    `ecall <n>`
- `ecall` does a few things
  - change to supervisor mode
  - start executing at a known point in kernel code
- kernel is expecting to receive control at that point in its code
- a bit involved, will discuss in a later lecture

Kernel: ***trusted computing base*** (TCB)

Aside: can one have process isolation WITHOUT h/w-supported

- supervisor/user mode and virtual memory?
- yes! use a strongly-typed programming language
  - For example, see Singularity O/S
  - but users can then use only approved languages/compilers
- still, h/w user/supervisor mode is the most popular plan

## Different Kernel Types

### Monolothic kernel

- kernel is a ***single big program*** implementing all system calls
- Xv6 does this.  Linux etc. too.
- kernel interface == system call interface
  - good: easy for subsystems to cooperate
    - one cache shared by file system and virtual memory
  - bad: interactions are complex
    - leads to bugs
    - no isolation within kernel for e.g. device drivers

### Microkernel design

- minimal kernel
  - ***IPC, memory, processes***
  - but *not* other system calls
- OS services run as ordinary user programs
  - FS, net, device drivers
  - so shell opens a file by sending msg thru kernel to FS service
  - kernel interface != system call interface
    - good: encourages modularity; limit damage from kernel bugs
    - bad: may be hard to get good performance

How common are kernel bugs?

- Common Vulnerabilities and Exposures web site
- https://cve.mitre.org/cgi-bin/cvekey.cgi?keyword=linux

Both monolithic and microkernel designs widely used

O/S kernels are an active area of development

- phone, cloud, embedded, iot, &c
- lwn.net

## xv6

Let's look at xv6 in particular

xv6 runs only on RISC-V CPUs

- and requires a specific setup of surrounding devices -- the board
- modeled on the "SiFive HiFive Unleashed" board
- hifive.pdf

- A simple board (e.g., no display)
  - RISC-V processor with 4 cores
  - RAM (128 MB)
  - UART for console
  - disk-like storage
  - ethernet
  - boards like this are pretty cheap, though not powerful
- Qemu emulates this CPU and a similar set of board devices
  - called "virt", as in "qemu -machine virt"
    - https://github.com/riscv/riscv-qemu/wiki
  - close to the SiFive board (https://www.sifive.com/boards)
    - but with virtio for disk

What's inside the RISC-V chip on this board?

- four cores, each with
  - 32 registers
  - ALU (add, mul, &c)
  - MMU
  - control registers
  - timer, interrupt logic
  - bus interface
- the cores are largely independent, e.g. each has its own registers
  - they share RAM
  - they share the board devices

### xv6 Kernel Source

To clean and prepare the kernel source, use the following commands:

```sh
% make clean
% ls kernel
```

For example, to explore the file system module within the kernel:

- File system code is located at `kernel/fs.c`.
- To view kernel modules and internal interfaces, open `kernel/defs.h`.

It is noted that xv6 is small enough for comprehensive understanding by the end of the semester, capturing key ideas while being much smaller than Linux.

### Building xv6

Building the xv6 operating system involves compiling the kernel source files and generating the kernel binary and a disk image containing the file system. Use the following commands:

```sh
% make
```

This command compiles each `.c` file in the kernel directory to `.o` files, links them, and generates the kernel binary at `kernel/kernel`. To examine the compiled kernel and its assembly, use:

```sh
% ls -l kernel/kernel
% more kernel/kernel.asm
```

The build process also produces a disk image:

```sh
% ls -l fs.img
```

### QEMU

To run xv6 with QEMU, which simulates a computer with the necessary hardware for xv6 to run:

```sh
% make qemu
```

QEMU loads the kernel binary into "memory" and simulates a disk with `fs.img`, executing the kernel's first instruction. It emulates hardware registers and RAM, interpreting the instructions executed by the kernel.

#### xv6 Bootup and Initial Process

The process from booting up xv6 to the execution of the first system call involves:

```sh
% make CPUS=1 qemu-gdb
% riscv64-unknown-elf-gdb
(gdb) b *0x80000000
(gdb) c
```

Kernel is loaded at `0x80000000` as this is where RAM starts, with lower addresses dedicated to device hardware.

The initialization sequence in xv6 involves setting up the stack for C function calls and transitioning to executing C code:

```sh
% vi kernel/entry.S
% vi start.c
```

Within the debugger (`gdb`), setting a breakpoint at `main` and stepping through the initialization processes reveals:

```sh
(gdb) b main
(gdb) c
(gdb) tui enable
```

The `main()` function is critical for setting up both software and hardware components. Initial kernel print statements can be examined using:

```sh
(gdb) step
```

#### Kernel Memory Allocator

A glance at the kernel memory allocator's initialization:

```sh
(gdb) step -- into kinit()
(gdb) step -- into freerange()
(gdb) step -- into free()
% vi kernel/kalloc.c
```

This section describes how `kinit` and `freerange` work together to identify and list pages of physical RAM, implementing a simple memory allocator.

#### Initializing Processes

To get the first C user-level program running, the process involves setting up the necessary structures and memory space for the `init` program:

```sh
% vi kernel/proc.c
% vi user/initcode.S
% vi kernel/syscall.h
```

The process of executing an `ecall` and transitioning from user to kernel space to handle system calls is detailed, highlighting the mechanics of the `exec` system call:

```sh
(gdb) b *0x0
(gdb) c
(gdb) tui disable
(gdb) x/10i 0
```

Further examination of the `exec` system call and the initialization of the top-level process is provided:

```sh
% vi kernel/exec.c
% vi user/init.c
```
