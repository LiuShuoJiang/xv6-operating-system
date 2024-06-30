# Lecture 11: Thread Switching

## Topic: More "Under the Hood" with xv6

### Previously

- System calls
- Interrupts
- Page tables
- Locks

### Today

- Process/Thread switching

## Why Support Concurrent Tasks?

- **Time-sharing**: Many users and/or many running programs.
- **Program structure**: Prime number sieve.
- **Parallel programming**: Exploit multi-core hardware.

## xv6 Involves Two Related Forms of Concurrency

- Processes
- Threads inside the kernel

## What's a Thread?

- The state required for an **independent serial execution**
  - ***Registers***, ***PC***, ***stack***
- Each thread, taken alone, executes in the ordinary way
- Typically multiple threads *share memory*
  - See each other's changes
  - So they need locks when they interact
- Usually more threads than CPUs
- Threading system multiplexes CPUs among threads
  - Today's main topic

## Threads Versus Processes?

- In xv6, a process has a single user-level thread and a single kernel thread to execute its system calls.
- ***There's sharing of data among xv6 kernel threads, but not at user level, thus the locks in the xv6 kernel.***

## Can One Have Multiple Threads in One Process at User Level?

- Not in xv6
- But yes in most operating systems, e.g., Linux, to support parallel programming at user level.

## Techniques Other Than Threads for Interleaving Multiple Tasks

- Look up *event-driven programming* or *state machines*
- Threads are not the most efficient, but they are convenient

## A Thread Might Be Executing, or Not

- **If executing**:
  - The thread is using the CPU and registers
  - It has a stack and memory
- **If not executing ("suspended", "blocked", "waiting")**:
  - Registers must be saved somewhere in memory
  - Stack and memory need to be preserved
- When a thread is suspended, and then resumed:
  - Must save state, and then restore it

## xv6 Process Saved State

```markdown
[Diagram]
  User Memory
  User Stack (C function call chain &c)
  User Saved Registers (in trapframe (TF))
  Kernel Stack
  Kernel Saved Registers ("context" (CTX))
  Kernel p->state
```

## "Kernel Thread" is a Process's Thread in the Kernel

- Each process has its own kernel thread

## If an xv6 Process is Not Executing

- It's typically waiting for some event -- console, pipe, disk, &c, because it made a system call into the kernel, e.g., `read()`.
- May be waiting midway through complex kernel system call implementation
  - Needs to resume in kernel system call code where it left off
  - Thus the saved kernel registers (and stack) as well as saved user registers

## It May Look Like Each xv6 Process Has Two Independent Threads

- One user, one kernel
- But really there's just one
  - If executing in kernel, user thread is waiting for trap return
  - If executing in user space, kernel thread is empty
- I'll use "process" and "thread" interchangeably

## `p->state` Values

- **`RUNNING`**: Executing on a CPU right now (either in user or kernel)
- **`RUNNABLE`**: Wants to execute, but isn't (suspended, in kernel)
- **`SLEEPING`**: Waiting for an event (suspended, in kernel `read()`, `wait()`, &c)
- `p->state` tells the scheduler how to handle the process
- Guards against mistakes like executing on two CPUs at the same time

## How xv6 Switches from One Process to Another -- "Context Switch"

- E.g., P1 calls `read()` and waits, P2 is `RUNNABLE`
- Diagram:

```markdown
[P1, TF1, KSTACK1, CTX1, swtch(),
 CTXs, STACKs, swtch(),
 CTX2, KSTACK2, TF2, P2]
```

- **TF** = trapframe = saved user registers
- **CTX** = context = saved kernel registers
- Getting from one user process to another involves multiple transitions:
  - **User -> Kernel**; saves user registers in trapframe
  - **Kernel thread -> Scheduler thread**; saves kernel thread registers in context
  - **Scheduler thread -> Kernel thread**; restores kernel thread registers from context
  - **Kernel -> User**; restores user registers from trapframe

## `struct proc` in `proc.h`

- `p->trapframe` holds saved user thread's registers
- `p->context` holds saved kernel thread's registers
- `p->kstack` points to the thread's kernel stack
- `p->state` is `RUNNING`, `RUNNABLE`, `SLEEPING`, &c
- `p->lock` protects `p->state` (and other things...)

## Why a Separate Scheduler Thread?

- So that there's always a stack on which to run scheduler loop
  - E.g., switching away from an exiting process
  - E.g., fewer processes than CPUs

## Scheduler Thread Details

- One per CPU; each has a stack and a `struct context`
- A kernel thread always switches to this CPU's scheduler thread, which switches to another kernel thread, if one is `RUNNABLE`
  - There are never direct process-to-process switches
- Each CPU's scheduler thread keeps scanning the process table until it finds a `RUNNABLE` thread
  - If there is no `RUNNABLE` thread, the scheduler is "idle"

## What if User Code Computes Without Ever Making a System Call?

- Will that stop other processes from running?
- CPU's timer hardware forces periodic interrupt
  - xv6 timer handler switches ( *"yields"* )
- "Pre-emption" or "involuntary context switch"

## Code: Pre-emptive Switch Demonstration

1. `vi user/spin.c`
2. Two CPU-bound processes
3. My qemu has only one CPU
4. We'll watch xv6 switch between them

### Commands

```sh
make qemu-gdb
gdb
(gdb) c
```

Show `user/spin.c`

```sh
spin
```

You can see that they alternate execution.

xv6 is switching its one CPU between the two processes, driven by timer interrupts.

We can see how often the timer ticks. How does the switching work?

## Breakpoint at Timer Interrupt

```sh
(gdb) b trap.c:81
(gdb) c
(gdb) tui enable
(gdb) where
```

We're in `usertrap()`, handling a device interrupt from the timer that occurred in user space.

`timerinit()` in `kernel/start.c` configures the RISC-V timer hardware.

### What Was Running When the Timer Interrupt Happened?

```sh
(gdb) p p->name
(gdb) p p->pid
(gdb) p/x p->trapframe->epc
```

Let's look for the saved `epc` in `user/spin.asm`.

Timer interrupted user code in the increment loop, no surprise.

```sh
(gdb) step into yield() in proc.c
(gdb) next
(gdb) p p->state
```

Change `p->state` from RUNNING to RUNNABLE -> give up CPU but want to run again.

Note: `yield()` acquires `p->lock` to prevent another CPU from running this `RUNNABL`E thread!

We're still using its kernel stack and we have not yet saved its kernel registers in its context.

```sh
(gdb) next ...
(gdb) step into sched()
```

`sched()` makes some sanity checks, then calls `swtch()`

```sh
(gdb) next (until about to call swtch())
```

About to context switch to this CPU's scheduler thread

- `swtch` will save the current RISC-V registers in the first argument (`p->context`) and restore previously-saved registers from the second argument (`c->context`).
- `c->context` are saved registers from this CPU's scheduler thread.
- `c == cpus[0]`, but `c` has been optimized away...

Let's see what register values `swtch()` will restore.

```sh
(gdb) p/x cpus[0]->context
```

Where is `cpus[0]->context.ra`?

```sh
(gdb) x/i cpus[0]->context.ra
```

`swtch()` will return to the `scheduler()` function in `proc.c`.

```sh
(gdb) tbreak swtch
(gdb) c
(gdb) tui disable
```

We're in `kernel/swtch.S`.

- `swtch()` saves current registers in `xx(a0)`, `a0` is the first argument, `p->context`.
- `swtch()` then restores registers from `xx(a1)`, `a1` is the second argument, `c->context`.

Then `swtch` returns.

### Questions

1. **Q:** `swtch()` neither saves nor restores `$pc` (program counter)! So how does it know where to start executing in the target thread?
2. **Q:** Why does `swtch()` save only 14 registers (`ra`, `sp`, `s0..s11`)? The RISC-V has 32 registers -- what about the other 18?
   - `zero`, `gp`, `tp`
   - `t0-t6`, `a0-a7`
   - Note we're talking about kernel thread registers, all 32 user register have already been saved in the trapframe.

### Registers at Start of `swtch`

```sh
(gdb) where
(g

db) p $pc  -- swtch
(gdb) p $ra  -- sched
(gdb) p $sp
```

### Registers at End of `swtch`

```sh
(gdb) stepi 28   -- until ret
(gdb) p $pc  -- swtch
(gdb) p $ra  -- scheduler
(gdb) p $sp  -- stack0+??? -- entry.S set this up at boot
(gdb) where
(gdb) stepi
(gdb) tui enable
```

We're in `scheduler()` now, in the "scheduler thread", on the scheduler's stack.

`scheduler()` just returned from a call to `swtch()`.

- It made that call a while ago, to switch to our process's kernel thread.
- That previous `swtch()` saved `scheduler()`'s registers.
- Our process's call to `swtch()` restored `scheduler()`'s saved registers.
- `p` here refers to the interrupted process.

```sh
(gdb) p p->name
(gdb) p p->pid
(gdb) p p->state
```

Remember `yield()` acquired the process's lock.

- Now `scheduler` releases it.
- The `scheduler()` code *looks* like an ordinary acquire/release pair, but in fact, `scheduler` acquires, `yield` releases, then `yield` acquires, `scheduler` releases.
- Unusual: the lock is released by a different thread than acquired it!

### Questions 2

1. **Q:** Why hold `p->lock` across `swtch()`?
   - `yield()` acquires
   - `scheduler()` releases
   - Could `sched()` release `p->lock` just before calling `swtch()`?
   - `p->lock` makes these steps atomic:
     - `p->state=RUNNABLE`
     - Save registers in `p->context`
     - Stop using `p's` kernel stack
     - So other CPU's scheduler won't start running `p` until all steps complete

`scheduler()`'s loop looks at all processes, finds one that's `RUNNABLE`, keeps looping until it finds something -- may be idle for a while.
In this demo, will find the other spin process.

### Questions 3

1. **Q:** Does `scheduler()` give `proc[0]` an unfair advantage?
   - Since its for loop always starts by looking at `proc[0]`?

Let's fast-forward to when `scheduler()` finds a `RUNNABLE` process.

```sh
(gdb) tbreak proc.c:463
(gdb) c
```

`scheduler()` locked the new process, sets state to `RUNNING`, now another CPU's scheduler won't run it.
`p` is the other "spin" process:

```sh
(gdb) p p->name
(gdb) p p->pid
(gdb) p p->state
```

Let's see where the new thread will start executing after `swtch()`.

By looking at `$ra` (return address) in its context.

```sh
(gdb) p/x p->context.ra
(gdb) x/i p->context.ra
```

New thread will return into `sched()`.

Look at `kernel/swtch.S` (again).

```sh
(gdb) tbreak swtch
(gdb) c
(gdb) stepi 28 -- now just about to execute swtch()'s ret
(gdb) p $ra
(gdb) where
```

Now we're in a timer interrupt in the *other* spin process.

In the past it was interrupted, called `yield()` / `sched()` / `swtch()`, but now it is resuming and will return to user space.

### Questions 4

1. **Q:** What is the "scheduling policy"? 
   - How does xv6 decide what to run next if multiple threads are `RUNNABLE`?
   - Is it a good policy?
2. **Q:** Is there pre-emptive scheduling of kernel threads?
   - As distinct from user threads, which we know can be pre-empted.
   - Yes -- timer interrupt and `yield()` can occur while in kernel.
   - `yield()` called by `kerneltrap()` in `kernel/trap.c`.
   - Where to save registers of interrupted kernel code?
     - Not in `p->trapframe`, since already has user registers.
     - Not in `p->context`, since we're about to call `yield()` and `swtch()`.
     - `kernelvec.S` pushes them on the kernel stack (since already in kernel).
   - Is pre-emption in the kernel necessary?
     - Maybe not.
     - Valuable if some system calls have lots of compute.
     - Or if we need a strict notion of thread priority.

### Questions 5

1. **Q:** Why does `scheduler()` enable interrupts, with `intr_on()`?
   - There may be no `RUNNABLE` threads.
   - They may all be waiting for I/O, e.g., disk or console.
   - Enable interrupts so the device has a chance to signal completion and thus wake up a thread.
   - Otherwise, system will freeze.

2. **Q:** Why does `sched()` comment say only `p->lock` can be held?
   - Suppose process P1, holding lock L1, yields CPU.
   - Process P2 runs, calls `acquire(L1)`.
   - P2's `acquire` spins with interrupts turned off.
   - So timer interrupts won't occur.
   - So P2 won't yield the CPU.
   - So P1 can't execute.
   - So P1 won't release L1, ever.

3. **Q:** Can we get rid of the separate per-CPU scheduler thread?
   - Could `sched()` directly `swtch()` to a new thread?
   - That would be faster -- avoids one of the `swtch()` calls.
   - We'd move `scheduler()`'s loop into `sched()`.
   - Maybe -- but:
     - Scheduling loop would run in `sched()` on a thread's kernel stack.
     - What if that thread is exiting?
     - What if another CPU wants to run the thread?
     - What if there are fewer threads than CPUs -- i.e., too few stacks?
     - Can be dealt with -- give it a try!

## Summary

- xv6 provides a convenient thread model for kernel code.
- Pre-emptive via timer interrupts.
- Transparent via switching registers and stack.
- Multi-core requires careful handling of stacks, locks.
- Next lecture: Mechanisms for threads to wait for each other.
