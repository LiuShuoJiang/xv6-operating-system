# Lecture 12: Coordination (`sleep` & `wakeup`)

## Plan

- Re-emphasize a few points about xv6 thread switching
- Sequence coordination
  - Sleep & wakeup
  - Lost wakeup problem
- Termination

## Why Hold `p->lock` Across `swtch()`?

This point affects many situations in xv6.

### `yield()`

```c
acquire(&p->lock);
p->state = RUNNABLE;
swtch();
```

### `scheduler()`

```c
swtch();
release(&p->lock);
```

### Two Reasons to Hold `p->lock` Across `swtch()`

1. Prevent another core's scheduler from seeing `p->state == RUNNABLE` until after the original core has stopped executing the thread and after the original core has stopped using the thread's stack.
2. Prevent timer interrupt from calling `yield()` during `swtch()` (remember: `acquire()` turns off interrupts). The second `swtch()` would overwrite already-saved registers in context.

## Why Does xv6 Never Hold a Spinlock When Yielding the CPU?

(Other than `p->lock`)

### On a Single-Core Machine, Imagine This

- **P1**:
  - `acq(Lx)`
  - `sched()`
- **P2**:
  - `acq(Ly)`
  - `acq(Lx)`

P2 will wait forever:

- P2 will spin waiting for P1 to release `Lx`
- P2 holds `Ly`, so it must keep interrupts off
- No clock interrupts, so P1 won't run
- So `Lx` won't ever be released

### Possible Even on Multi-Core, with More Locks/Threads

- Solution: forbid holding lock when yielding the CPU (other than `p->lock`)

## Topic: Sequence Coordination

Threads often wait for specific events or conditions:

- Wait for disk read to complete (event is from an interrupt)
- Wait for pipe writer to produce data (event is from a thread)
- Wait for a child to exit

### Coordination is a Fundamental Building-Block for Thread Programming

But subject to rules that can present puzzles.

### Why Not Just Have a While-Loop That Spins Until Event Happens?

- Pipe read:

  ```c
  while (buffer is empty) {
  }
  ```

- Pipe write:

  ```c
  put data in buffer
  ```

### Better Solution: Coordination Primitives That Yield the CPU

- There are a bunch, e.g., barriers, semaphores, event queues.
- xv6 uses sleep & wakeup (used by many systems, similar to "condition variables").

## Example: `uartwrite()` and `uartintr()` in `uart.c`

- Recall: the UART is the device hardware that connects to the console.
- These are simplified versions of xv6 code.

### Basic Idea

- The UART hardware accepts one byte of output at a time (really a few).
- Hardware takes a long time to send each byte, perhaps a millisecond.
- Processes writing to the console must wait until UART sends the previous character.
- The UART hardware interrupts after it has sent each character.
- Writing thread should give up the CPU until then.

### `write()` Calls `uartwrite()`

- `uartwrite()` writes the first byte (if it can).
- `uartwrite()` calls `sleep()` to wait for the UART's interrupt.
- `uartintr()` calls `wakeup()`.
- The `&tx_chan` argument serves to link the sleep and wakeup.

### Simple and Flexible

- `sleep/wakeup` don't need to understand what you're waiting for.
- No need to allocate explicit coordination objects.

## Why the Lock Argument to `sleep()`?

Sadly, you cannot design `sleep()` as cleanly as you might hope. `sleep()` cannot simply be "wait for this event". The problem is called "lost wakeups". It lurks behind all sequence coordination schemes and is a pain.

### Here's the Story

#### Suppose Just `sleep(chan)`; How Would We Implement?

Here's a BROKEN `sleep/wakeup`

##### `broken_sleep(chan)`

- Sleeps on a "channel", a number/address that identifies the condition/event we are waiting for.

  ```c
  p->state = SLEEPING;
  p->chan = chan;
  sched();
  ```

##### `wakeup(chan)`

- Wakes up all threads sleeping on `chan`.
- May wake up more than one thread.
- For each `p`:

  ```c
  if (p->state == SLEEPING && p->chan == chan) {
    p->state = RUNNABLE;
  }
  ```

- (I've omitted `p->lock`, which both need).

### How Would UART Code Use This (Broken) `sleep/wakeup`?

```c
int busy;
int chan;

uartwrite(buf) {
  for (each char c) {
    while (busy) {
      sleep(&chan);
    }
    send c;
    busy = 1;
  }
}

uartintr() {
  busy = 0;
  wakeup(&chan);
}
```

- `busy == 0` is the condition we're waiting for.
- `&chan` is the sleep channel (a dummy variable).

## But What About Locking?

- Driver's data structures, e.g., `busy`.
- UART hardware.

### Both `uartwrite()` and `uartintr()` Need to Lock

Should `uartwrite()` hold a lock for the whole sequence?

- No: then `uartintr()` can't get lock and clear `busy`.

### Maybe `uartwrite()` Could Release the Lock Before `sleep()`?

Let's try it—modify `uart.c` to call `broken_sleep()`:

```c
release(&uart_tx_lock);
broken_sleep(&tx_chan);
acquire(&uart_tx_lock);
```

Make `qemu`; `cat README`.

### What Goes Wrong When `uartwrite()` Releases the Lock Before `broken_sleep()`?

- `uartwrite()` saw that the previous character wasn't yet done being sent.
- Interrupt occurred after `release()`, before `broken_sleep()`.
- `uartwrite()` went to sleep EVEN THOUGH UART TX WAS DONE.
- Now there is nothing to wake up `uartwrite()`, it will sleep forever (really, until next UART interrupt, due to input).

### This is the "Lost Wakeup" Problem

We need to eliminate the window between `uartwrite()`'s check of the condition, and `sleep()` marking the process as asleep. We'll use locks to prevent `wakeup()` from running during the entire window.

### We'll Change the `sleep()` Interface and the Way It's Used

We'll require that there be a lock that protects the condition and require that the callers of both `sleep()` and `wakeup()` hold this "condition lock".

#### code: `sleep(chan, lock)`

- Caller must hold lock.
- `sleep` releases lock, re-acquires before returning.

#### code: `wakeup(chan)`

- Caller must hold lock.
- (Repair `uart.c`).

## Let's Look at `wakeup(chan)` in `proc.c`

- It scans the process table, looking for `SLEEPING` and `chan`.
- It grabs each `p->lock`.
- Remember also that caller acquired condition lock before calling `wakeup()`.
- So `wakeup()` holds BOTH the condition lock and each `p->lock`.

### Q: Why is it Enough to Just Set `p->state = RUNNABLE`?

Why does that cause the thread to run?

## Let's Look at `sleep()` in `proc.c`

- `sleep` *must* release the condition lock since we can't hold locks when calling `swtch()`, other than `p->lock`.

### Q: How to Prevent `wakeup()` from Running After `sleep()` Releases the Condition Lock?

- A: Acquire `p->lock` before releasing condition lock.
  - Since `wakeup()` holds *both* locks, it's enough for `sleep()` to hold *either* in order to force `wakeup()` to spin rather than look at this process.
  - Now `wakeup()` can't proceed until after `swtch()` completes, so `wakeup()` is guaranteed to see `p->state == SLEEPING` and `p->chan == chan`.
  - Thus: no lost wakeups!

## Note that `uartwrite()` Wraps the `sleep()` in a Loop

- i.e., re-checks the condition after `sleep()` returns, may sleep again.
- Two reasons:
  1. Maybe multiple waiters, another thread might have consumed the event.
  2. `kill()` wakes up processes even when the condition isn't true.

All uses of `sleep` are wrapped in a loop, so they re-check.

## Another Example: `piperead()` in `pipe.c`

- The condition is data waiting to be read (`nread != nwrite`).
- `pipewrite()` calls `wakeup()` at the end.

### What is the Race if `piperead()` Used `broken_sleep()`?

Lost wakeups are not just about interrupts!

### Note the Loop Around `sleep()`

- Multiple processes may be reading the same pipe.
- Why the `wakeup()` at the end of `piperead()`?

## The `sleep/wakeup` Interface/Rules are a Little Complex

- In particular, `sleep()` needs the condition lock.
- But `sleep()` doesn't need to understand the condition.
- `sleep/wakeup` is pretty flexible, though low-level.
- There are other schemes that are cleaner but perhaps less general-purpose, e.g., the counting semaphore in today's reading.
- All have to cope

with lost wakeups, one way or another.

## Another Coordination Challenge: How to Terminate a Thread?

### Problem: Thread X Cannot Just Destroy Thread Y

- What if Y is executing on another core?
- What if Y holds locks?
- What if Y is in the middle of a complex update to important data structures?

### Problem: A Thread Cannot Free All of Its Own Resources

- e.g., its own stack, which it is still using.
- e.g., its `struct context`, which it may need to call `swtch()`.

### xv6 Has Two Ways to Get Rid of Processes: `exit()` and `kill()`

### Ordinary Case: Process Voluntarily Quits with `exit()` System Call

The strategy:
- `exit()` frees some things, but not `proc` slot or stack.
- Parent's `wait()` does the final frees.
The goal:
- `p->state = UNUSED`, so a future `fork()` can use this `proc[]` slot.

#### `exit()` in `proc.c`

- Some cleanup.
- Wake up `wait()`ing parent.
- `p->state = ZOMBIE`.
  - `ZOMBIE` means ready for parent's `wait()`.
  - Not `UNUSED`—so won't be allocated by a `fork()`.
  - Won't run again.
  - (Note stack and `proc[]` entry are still allocated...)
- `swtch()` to scheduler.

#### `wait()` in `proc.c` (Parent Will Eventually Call)

- `sleep()`s waiting for any child exit().
- Scans `proc[]` table for children with `p->state == ZOMBIE`.
- Calls `freeproc()`.
  - (`p->lock` held...)
  - `trapframe`, `pagetable`, ..., `p->state = UNUSED`.

Thus: `wait()` is not just for app convenience, but for OS as well. Every process must be `wait()`ed for.

Some complexity due to:

- Wait-then-exit versus exit-then-wait.
- What if the parent has already exited?

### What About `kill(pid)`?

#### Problem: May Not Be Safe to Forcibly Terminate a Process

- It might be executing in the kernel.
  - Using its kernel stack, page table, `proc[]` entry, `trapframe`.
- It might hold locks.
  - e.g., in the middle of forking a new process.
  - And must finish to restore invariants.

#### Solution

- `kill()` sets `p->killed` flag, nothing else.
- The target process itself checks for `p->killed` and calls `exit()` itself.
  - Look for `if (p->killed) exit(-1);` in `usertrap()`.
    - Not in the middle of anything here, and no locks are held.
    - So it's safe to exit.

### What if `kill()` Target is `sleep()`ing?

- In that case, it doesn't hold locks and isn't executing!
- Is it OK for `kill()` to destroy the target right away?
  - No: what if waiting for disk midway through file creation?

#### xv6 Solution to `kill()` of `sleep()`ing Process

- See `kill()` in `proc.c`.
  - Changes `SLEEPING` to `RUNNABLE`—like `wakeup()`.
  - So `sleep()` will return, probably before condition is true.

Some sleep loops check for `p->killed`, e.g., `piperead()`, `consoleread()`. Otherwise, read could hang indefinitely for a killed process. Some sleep loops don't check `p->killed`, e.g., `virtio_disk.c`. It's OK not to check `p->killed` since disk reads are pretty quick.

So a killed process may continue for a while, but `usertrap()` will exit() after the system call finishes.

### xv6 Spec for Kill

- If target is in user space:
  - Will die next time it makes a system call or takes a timer interrupt.
- If target is in the kernel:
  - Target will never execute another user instruction but may spend a while yet in the kernel.

## Summary

- `sleep/wakeup` let threads wait for specific events.
- Concurrency means we have to worry about lost wakeups.
- Termination is a pain in threading systems.
