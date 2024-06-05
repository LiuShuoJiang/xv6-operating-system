# Lecture 1: OS overview

## About

This course is about:

- OS design
- Hands-on experience

## Overview

What are the purposes of an OS?

- ***Multiplex*** the hardware among many applications
- ***Isolate*** applications for ***security*** and to contain bugs
- Allow ***sharing*** among cooperating applications
- ***Abstract*** the hardware for portability
- Provide a range of useful and ***high-performance*** services

What's an operating system? [user/kernel diagram]

- **User** applications: sh (shell), cc (C compiler), DB, &c
- **System calls**:
  - `fd = open("out", 1);`
  - `write(fd, "hello\n", 6);`
  - `pid = fork();`
  - and more...
- **Kernel** services: ***file system***, ***processes***, ***memory***, access control, network, &c
- Hardware: CPU, RAM, disk, net, &c

Design tensions make O/S design interesting:

- efficient vs abstract/portable/general-purpose
- powerful machinery vs simple interfaces
- flexible vs secure
- compatible vs new hardware and interfaces

You'll be glad you took this course if you...

- are curious about how computer systems work
- need to track down bugs or security problems
- care about high performance

## Class Structure

Online course information:

- website: <https://pdos.csail.mit.edu/6.828/2023/schedule.html>

Lectures:

- OS ideas
- case study of xv6, a small O/S, via code and xv6 book
- lab background
- OS papers

Labs:

- The point: hands-on experience
- Mostly one week each.
- Three kinds:
  - Systems programming (due next week...)
  - O/S primitives, e.g. thread switching.
  - O/S kernel extensions to xv6, e.g. network.

## Introduction to UNIX System Calls

Applications interact with the O/S via "system calls."

- you'll use system calls in the first lab.
- and extend and improve them in subsequent labs.

I'll show some application examples, and run them on xv6.

- xv6 is an OS we created specifically for this class.
- xv6 is patterned after **UNIX**, but far simpler.
  - UNIX is the ancestor, at some level, of many of today's OSs.
    - e.g. Linux and MacOS.
  - learning about xv6 will help you understand many other OSs.
- you'll be able to digest all of xv6 -- no mysteries.
  - accompanying book explains how xv6 works, and why
- xv6 has two roles in 6.1810:
  - example of core mechanisms
  - starting point for most of the labs
- xv6 runs on a **RISC-V** CPU, as in 6.1910 (6.004)
- you'll run xv6 inside the qemu machine emulator

## Examples

### Example: ex1.c, copy input to output

> Refer to: [copy.c](../user/copy.c)

- Read bytes from input, write them to the output

```shell
% make qemu
$ ex1
```

- You can find these example programs via the schedule on the web site
- `ex1.c` is written in C
  - C is probably the most common language for OS kernels
- `read()` and `write()` are system calls
  - They look like function calls, but actually jump into the kernel.
- First `read()`/`write()` argument is a "***file descriptor***" (FD)
  - Passed to kernel to tell it which "open file" to read/write
  - Must previously have been opened
  - An FD connects to a file/pipe/socket/&c
  - A process can open many files, have many FDs
- UNIX convention: *fd 0 is "standard input", 1 is "standard output"*
  - Programs don't have to know where input comes from, or output goes
  - They can just read/write FDs 0 and 1
- Second `read()` argument is a pointer to some memory into which to read
- Third argument is the number of bytes to read
  - `read()` may read less, but not more
- Return value: number of bytes actually read, or -1 for error
- Note: `ex1.c` does not care about the format of the data
  - UNIX I/O is 8-bit bytes
  - Interpretation is application-specific, e.g. database records, C source, &c
- Where do file descriptors come from?
  - They are returned by `open()`, `pipe()`, `socket()`, &c

### Example: ex2.c, create a file

> Refer to: [open.c](../user/open.c)

- `open()` creates (or opens) a file, returns a file descriptor (or -1 for error)
- FD is a small integer
- FD indexes into a **per-process table** maintained by kernel
- [user/kernel diagram]

```shell
$ ex2
$ cat out
```

- Different processes have different FD name-spaces
  - e.g. FD 3 usually means different things to different processes
- These examples ignore errors -- don't be this sloppy!
- Figure 1.2 in the xv6 book lists system call arguments/return
  - Or look at UNIX man pages, e.g. `man 2 open`

#### What happens when a program calls a system call like `open()`?

- Looks like a function call, but it's actually a special instruction
- CPU saves some user registers
- CPU increases privilege level
- CPU jumps to a known "entry point" in the kernel
- Now running C code in the kernel
- Kernel calls system call implementation
  - `sys_open()` looks up name in file system
  - It might wait for the disk
  - It updates kernel data structures (file block cache, FD table)
- Restore user registers
- Reduce privilege level
- Jump back to calling point in the program, which resumes
- We'll see more detail later in the course

#### The Shell

I've been typing to UNIX's command-line interface, the shell.

- The shell prints the `$` prompts.
- The shell is an ordinary user program, not part of the kernel.
- The shell lets you run UNIX command-line utilities
  - Useful for system management, messing with files, development, scripting

```shell
$ ls
$ ls > out
$ grep x < out
```

- UNIX supports other styles of interaction too
  - GUIs, servers, routers, &c.
- But time-sharing via the shell was the original focus of UNIX.
- We can exercise many system calls via the shell.

### Example: ex3.c, create a new process

> Refer to: [fork.c](../user/fork.c)

- The shell creates a new process for each command you type, e.g. for

```shell
$ echo hello
```

- A separate process helps prevent them from interfering, e.g. if buggy
- The `fork()` system call creates a new process
- The kernel makes a **copy** of the calling process
  - Instructions, data, registers, file descriptors, current directory
  - "parent" and "child" processes
- So child and parent are initially identical!
  - Except: `fork()` returns a pid in parent, 0 in child
- A pid (process ID) is an integer; kernel gives each process a different pid

```shell
$ ex3
```

- Thus:
  - `ex3.c`'s `"fork() returned"` executes in *both* processes
  - The `"if (pid == 0)"` allows code to know if it's parent or child
- Ok, `fork()` lets us create a new process
  - How can we run a program in that process?

### Example: ex4.c, replace calling process with an executable file

> Refer to: [exec.c](../user/exec.c)

- How does the shell run a program, e.g.

```shell
$ echo a b c
```

- A program is stored in a file: instructions and initial memory
  - Created by the compiler and linker
- So there's a file called `echo`, containing instructions
  - On your own computer: `ls -l /bin/echo`
- `exec()` replaces current process with an executable file
  - Discards old instruction and data memory
  - Loads instructions and initial memory from the file
  - Preserves file descriptors

```shell
$ ex4
```

- `exec(filename, argument-array)`
  - `argument-array` holds command-line arguments; `exec` passes to `main()`
  - `cat user/echo.c`
  - `echo.c` shows how a program looks at its command-line arguments

### Example: ex5.c, `fork()` a new process, `exec()` a program

> Refer to: [forkexec.c](../user/forkexec.c)

- The shell can't simply call `exec()`!
  - Since it wouldn't be running any more
  - Wouldn't be able to accept more than one command
- `ex5.c` shows how the shell deals with this:
  - `fork()` a child process
  - Child calls `exec()`
  - Parent `wait()`s for child to finish

```shell
$ ex5
```

- The shell does this `fork`/`exec`/`wait` for every command you type
  - After `wait()`, the shell prints the next prompt
- `exit(status)` -> `wait(&status)`
  - `status` allows children to send back 32 bits of info to parent
  - Status convention: 0 = success, 1 = command encountered an error
- Note: the `fork()` copies, but `exec()` discards the copied memory
  - This may seem wasteful
  - You'll transparently eliminate the copy in the "copy-on-write" lab

### Example: ex6.c, redirect the output of a command

> Refer to: [redirect.c](../user/redirect.c)

- What does the shell do for this?

```shell
$ echo hello > out
```

- Answer: `fork`, child changes FD 1, child `exec`'s `echo`

```shell
$ ex6
$ cat out
```

- Note: `open()` always chooses lowest unused FD; 1 due to `close(1)`.
- Note: `exec` preserves FDs
- `fork`, FDs, and `exec` interact nicely to implement I/O redirection
  - Separate `fork`-then-`exec` gives child a chance to change FDs
    - Before `exec()` gives up control
    - And without disturbing parent's FDs
- FDs provide indirection
  - Commands just use FDs 0 and 1, don't have to know where they go
- Thus: only `sh` knows about I/O redirection, not each program

#### Design Decisions

It's worth asking "why" about design decisions:

- Why these I/O and process abstractions? Why not something else?
- Why provide a file system? Why not let programs use the disk their own way?
- Why FDs? Why not pass a filename to `write()`?
- Why not combine `fork()` and `exec()`?
- The UNIX design works well, but we will see other designs!

### Example: ex7.c, communicate through a pipe

> Refer to: [pipetest.c](../user/pipetest.c)

- How does the shell implement

```shell
$ ls | grep x
```

- An FD can refer to a "pipe", rather than a file
- The `pipe()` system call creates two FDs
  - ***Read from the first FD***
  - ***Write to the second FD***
- The kernel maintains a buffer for each pipe
  - [u/k diagram]
  - `write()` appends to the buffer
  - `read()` waits until there is data

```shell
$ ex7
```

### Example: ex8.c, communicate between processes

> Refer to: [pipetestfork.c](../user/pipetestfork.c)

- Pipes combine with `fork()` to allow parent/child to communicate
  - Create a pipe,
  - `fork`,
  - Child writes to one pipe fd,
  - Parent reads from the other.
  - [diagram]

```shell
$ ex8
```

- The shell builds pipelines by forking twice and calling `exec()`
- Pipes are a separate abstraction, but combine well w/ `fork()`

### Example: ex9.c, list files in a directory

> Refer to: [lstest.c](../user/lstest.c)

- How does `ls` get a list of the files in a directory?
- You can open a directory and read it -> file names
- "." is a pseudo-name for a process's current directory

```shell
$ ex9
```

- See `ls.c` for more details
