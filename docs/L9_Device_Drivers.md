# Lecture 9: Device Drivers

## Topic: Device Drivers

- A CPU needs external devices: storage, communication, etc.
  - OS is in charge of programming the devices.
- New issues/complications:
  - Devices often have rigid and complex interfaces.
  - Devices and CPU run in parallel -- concurrency.
  - Interrupts:
    - Hardware wants attention now! (e.g., packet arrived).
    - Software must set aside current work and respond.
    - On RISC-V, use the same trap mechanism as for syscalls and exceptions.
    - Interrupts can arrive at awkward times.
- Most code in production OSes is device drivers.
  - You will write one for a network card.

## Where are Devices?

```C
[CPU, bus, RAM, uart/disk/net/etc, PLIC, interrupts]
```

## Programming Devices: Memory-mapped I/O

- Device controller has an address range.
- Load/store to these addresses read/write control registers.
- Platform designer decides where devices live in physical memory space.

### Example Device: UART

- Universal Asynchronous/Synchronous Transmitter.
- Serial interface, input, and output.
- "RS232 port."
- QEMU emulates the very common 16550 chip.
  - Many vendors, many "data sheets" on the web.
  - 16550.pdf.
  - Data sheet details physical, electrical, and programming.

```
[rx wire, receive shift register, FIFO]
[transmit FIFO, transmit shift register, tx wire]
```

- 16-byte FIFOs.
- Memory-mapped 8-bit registers at physical address `UART0=0x10000000`:
  - (page 9 of 16550.pdf)

  ```
  0: RHR / THR -- receive/transmit holding register.
  1: IER -- interrupt enable register, RX_ENABLE=0x1 and TX_ENABLE=0x2.
  ...
  5: LSR -- line status register, RX_READY=0x1 and TX_IDLE=0x20.
  ```

### How does a Kernel Device Driver Use These Registers?

- Simple example: `uartgetc()` in `kernel/uart.c`

```c
ReadReg(RHR) turns into *(char*)(0x10000000 + 0)
```

### Why Does the UART Have FIFOs?

- Device driver must cope with times when the device is not ready:
  - `read()` but rx FIFO is empty.
  - `write()` but tx FIFO is full.

### How Should Device Drivers Wait?

- Perhaps a "busy loop":

  ```c
  while((LSR & 1) == 0)
    ;
  return RHR;
  ```

  - OK if waiting is unlikely -- if input nearly always available.
  - Thus not OK for the console!
  - Often no input (keystrokes) are waiting in FIFO.
  - Most devices are like this -- may need to wait a long time for I/O.

### The Solution: Interrupts

- When the device needs driver attention, the device raises an interrupt.
- UART interrupts if:
  - rx FIFO goes from empty to not-empty, or
  - tx FIFO goes from full to not-full.

### How Does Kernel See Interrupts?

```
device -> PLIC -> trap -> usertrap()/kerneltrap() -> devintr()
```

- `trap.c devintr()`
- `scause` high bit indicates the trap is from a device interrupt.
- A PLIC register indicates which device interrupted.
  - The "IRQ" -- UART's IRQ is 10.
  - IRQs are defined by the "board" -- QEMU in this case.

### An Interrupt is Usually Just a Hint that Device State Might Have Changed

- The real truth is in the device's status registers.
  - Device driver must check them to decide action, if any.
- For UART, check LSR to see if rx FIFO non-empty, tx FIFO non-full.
  - `uart.c uartintr()`

## Typical Device Driver Structure

top half:

- executing a process's system call, e.g., write() or read().
- starts I/O, perhaps waits.

bottom half:

- the interrupt handler.
- reads input, or sends more output, from/to device hardware.
- needs to interact with "top half" process.
  - put input where the top half can find it.
  - tell it more input has arrived.
  - or that more output can be sent.
- does *not* run in the context of the top-half process.
  - maybe on different core.
  - maybe interrupting some other process.
- so interactions must be arms-length -- buffers, sleep/wakeup.

## Let's Look at How xv6 Sets Up the Interrupt Machinery

### Registers That Control Interrupts

- `sie` --- supervisor interrupt enabled register:
  - One bit per software interrupt, external interrupt, timer interrupt.
- `UART IER`.
- `PLIC claim` --- get next IRQ; acknowledge IRQ.
- `sstatus` --- supervisor status register, SIE bit enables interrupts.

### xv6's Interrupt Setup Code

- `start()`:

  ```c
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
  ```

- `main()`:

  ```c
  consoleinit();
    uartinit();
  plicinit();
  scheduler();
    intr_on();
      w_sstatus(r_sstatus() | SSTATUS_SIE);
  ```

### Let's Look at the Shell Reading Input from the Console/UART

```sh
% make qemu-gdb
% gdb
(gdb) c
(gdb) break sys_read
(gdb) c
(gdb) tui enable
```

### `sys_read()`

```c
sys_read()
    fileread();
        consoleread();
            this is the top half.
            look at cons.buf, cons.r, cons.w -- "producer/consumer buffer".
            [diagram: buf, r, w]
            sleep();
```

### Now for the Bottom Half

```sh
(gdb) break kerneltrap
(gdb) c
<press return>
```

### How Did We Get Here?

```sh
(gdb) where
```

- In kernel; no process wanted to run; `scheduler()`.
- `UART -> PLIC -> stvec -> kernelvec`

```sh
(gdb) p/x $stvec
(gdb) p kernelvec
```

### kernelvec.S

- `kernelvec` like trampoline, but for traps from the kernel.
  - Simpler: stack is valid, page table already kernel.
  - So can just save registers on stack, jump to `kerneltrap`.
- Save registers on current stack; which stack?
  - In this case, special scheduler stack.
  - If executing system call in kernel, some proc's stack.

### `kerneltrap()`

```
devintr();
  (gdb) p/x $scause
  scause high bit means it's an interrupt.
    p. 85 / Table 4.2 in RISC-V manual.
  plic_claim() to find IRQ (which device).
  (gdb) p irq
  uartintr();
    uartgetc();
    x/1bx 0x10000005;
    check LSR for rx, copy from RHR to buffer, wake up.
    consoleintr();
      print cons;
      print cons.buf[cons.r];
      wakeup();
    x/1bx 0x10000005 -- note low bit no longer set.
  plic_complete(irq);
return through devintr, kerneltrap, kernelvec.
```

```sh
(gdb) b *0x80005b92 -- the end of kernelvec
```

### Scheduler Will Now Run the Top Half -- sh's read()

- Since woken up.
- Let's break in top half -- `consoleread()`.

```sh
(gdb) b console.c:99
(gdb) c
(gdb) where
```

- `consoleread()`'s sleep() returns.
- `consoleread()` sees our character in `cons.buf[cons.r]`.
- sh's read returns, with my typed character.

## What If Multiple Devices Want to Interrupt at the Same Time?

- The PLIC distributes interrupts among cores.
  - Interrupts can be handled in parallel on different cores.
- If no CPU claims the interrupt, the interrupt stays pending.
  - Eventually, each interrupt is delivered to some CPU.

## What If Kernel Has Disabled Interrupts When a Device Asks for One?

- By clearing SIE in sstatus, with `intr_off()`.
- PLIC/CPU remember pending interrupts.
- Deliver when kernel re-enables interrupts.

## Interrupts Involve Several Forms of Concurrency

1. Between device and CPU:
   - Producer/consumer parallelism.
2. If enabled, interrupts can occur between any two instructions!
   - Disable interrupts when code must be atomic.
3. Interrupt may run on a different CPU in parallel with top half.
   - Locks: next lecture.

### Producer/Consumer Parallelism

- For input:
  - Can arrive at a time when the reader is not waiting.
  - Can arrive faster, or slower, than the reader can read.
  - Want to accumulate input and read() in batches for efficiency.
- For output:
  - If the device is slow, want to buffer output so the process can continue.
  - If the device is fast, want to send in batches for efficiency.
- A common solution pattern:
  - Producer/consumer buffer.
  - Notification.

### We've Seen This at Two Levels

1. UART internal FIFOs, for device and driver -- plus interrupts.
2. cons.buf, for top-half and bottom-half -- plus sleep/wakeup.

- We'll see this again when we look at pipes.

### If Enabled, an Interrupt Can Occur Between Any Two Instructions

- Example:
  - Suppose the kernel is counting something in a global variable n.
  - Top half: `n = n + 1`.
  - Interrupt bottom half: `n = n + 1`.
  - The machine code for `n=n+1` looks like this:

    ```c
    a4 = &n;
    lw a5,0(a4);
    addw a5,a5,1;
    sw a5,0(a4);
    ```

  - What if an interrupt occurs between `lw` and `addw`?
    - And the interrupt handler also says `n = n + 1`?
- One solution: briefly disable interrupts in top half.

  ```c
  intr_off();
  n = n + 1;
  intr_on();
  ```

  - `intr_off()`: `w_sstatus(r_sstatus() & ~SSTATUS_SIE);`

### RISC-V Automatically Turns Off Interrupts on a Trap (Interrupt/Exception/Syscall)

- Trampoline cannot tolerate a second interrupt to trampoline!
  - e.g., would overwrite registers saved in `trapframe`.
- More on this when we look at locking.

## Interrupt Evolution

- Interrupts used to be cheap in CPU cycles; now they take many cycles.
  - Due to pipelines, large register sets, cache misses.
  - Interrupt overhead is around a microsecond -- excluding actual device driver code!
- So:
  - Old approach: simple hardware, smart software, lots of interrupts.
  - New approach: smart hardware, does lots of work for each interrupt.

### What If Interrupt Rate is Very High?

- Example: modern ethernet can deliver millions of packets/second.
- At that rate, a big fraction of CPU time is in interrupt *overhead*.

## Polling: An Event Notification Strategy for High Rates

- Top-half loops until the device says it is ready.
  - e.g., `uartputc_sync()`.
  - Or perhaps check in some frequently executed kernel code, e.g., `scheduler()`.
- Then process everything accumulated since the last poll.
- More efficient than interrupts if the device is usually ready quickly.
- Perhaps switch strategies based on measured rate.

## DMA (Direct Memory Access) Can Move Data Efficiently

- The xv6 UART driver reads bytes one at a time in software.
  - CPUs are not very efficient for this: off chip, not cacheable, 8 bits at a time.
- But OK for low-speed devices.
- Most devices automatically copy input to RAM -- DMA.
  - Then interrupt.
  - Input is already in ordinary memory.
  - CPU memory operations are usually much more efficient.

## Interrupts and Device Handling a Continuing Area of Concern

- Special fast paths.
- Clever spreading of work over CPUs.
- Forwarding of interrupts to user space.
  - For page faults and user-handled devices.
  - Hardware delivers directly to user, without kernel intervention?
  - Faster forwarding path through kernel?
- We will be seeing these topics later in the course.

