# Lecture 10: Locking

## Why talk about locking?

- Apps want to use multi-core processors for parallel speed-up
- Kernel must deal with parallel system calls
- Parallel access to kernel data (buffer cache, processes, etc.)
- Locks help with correct sharing of data
- Locks can limit parallel speedup

## What goes wrong if we don't have locks?

### Case study: delete acquire/release in `kalloc.c`

- **Boot:**
  - Kernel works!
- **Run `usertests`:**
  - Many passes!
  - Some crash and we lose some pages

#### Why do we lose pages?

- **Picture of shared-memory multiprocessor**
- Race between two cores calling `kfree()`

#### BUMMER

- We need locks for correctness
- But lose performance (`kfree` is serialized)

## The lock abstraction

- **`Lock l`**
  - `acquire(l)`

  ```c
  x = x + 1 // "critical section"
  ```

  - `release(l)`

Notes:

- A lock is itself an object
- If multiple threads call `acquire(l)`
  - Only one will return right away
  - The others will wait for `release()` -- "block"
- A program typically has lots of data, lots of locks
  - If different threads use different data,
  - Then they likely hold different locks,
  - So they can execute in parallel -- get more work done.

- Note that lock `l` is not specifically tied to data `x`
  - The programmer has a plan for the correspondence

## A conservative rule to decide when you need to lock

- Any time two threads use a memory location, and at least one is a write
- Don't touch shared data unless you hold the right lock!
  - (Too strict: program logic may sometimes rule out sharing; lock-free)
  - (Too loose: `printf();` not always simple lock/data correspondence)

## Could locking be automatic?

- Perhaps the language could associate a lock with every data object
  - Compiler adds `acquire/release` around every use
  - Less room for programmer to forget!

- That idea is often too rigid:

  ```c
  if present(table1, key1):
    add(table2, key1)
  ```

  - **Race:**
    - Another thread may remove `key1` from `table1`
    - Before this thread gets a chance to add to `table2`

  - **We need:**

    ```
    lock table1
    lock table2
      present(..)
      add()
    unlock table1
    unlock table2
    ```

  - That is, programmer often needs explicit control over
    - The region of code during which a lock is held
    - In order to hide awkward intermediate states

## Ways to think about what locks achieve

- **Locks help avoid lost updates**
- **Locks help you create atomic multi-step operations** -- hide intermediate states
- **Locks help operations maintain invariants on a data structure**
  - Assume the invariants are true at start of operation
  - Operation uses locks to hide temporary violation of invariants
  - Operation restores invariants before releasing locks

## Problem: deadlock

- **Notice rename() held two locks**
- **What if:**

  ```c
  Core A              Core B
  rename(d1/x, d2/y)  rename(d2/a, d1/b)
    lock d1             lock d2
    lock d2 ...         lock d1 ...
  ```

### Solution

- Programmer works out an order for all locks
- All code must acquire locks in that order
- i.e. predict locks, sort, acquire -- complex!

## Locks versus modularity

- Locks make it hard to hide details inside modules
- To avoid deadlock, I need to know locks acquired by functions I call
- And I may need to acquire them before calling, even if I don't use them
- i.e. locks are often not the private business of individual modules

## Locks and parallelism

- **Locks *prevent* parallel execution**
- To get parallelism, you often need to split up data and locks
  - In a way that lets each core use different data and different locks
  - "Fine grained locks"
  
- Choosing best split of data/locks is a design challenge
  - Whole FS; directory/file; disk block
  - Whole kernel; each subsystem; each object

- You may need to re-design code to make it work well in parallel
  - Example: break single free memory list into per-core free lists
    - Helps if threads were waiting a lot on lock for single free list
  - Such re-writes can require a lot of work!

## Lock granularity advice

- Start with big locks, e.g. one lock protecting entire module
  - Less deadlock since less opportunity to hold two locks
  - Less reasoning about invariants/atomicity required
- Measure to see if there's a problem
  - Big locks are often enough -- maybe little time spent in that module
- Re-design for fine-grained locking only if you have to

## Let's look at locking in xv6

### A typical use of locks: `uart.c`

- Typical of many O/S's device driver arrangements

#### Diagram

- User processes, kernel, UART, `uartputc`, remove from `uart_tx_buf`, `uartintr()`

- **Sources of concurrency:** processes, interrupt
- **Only one lock in `uart.c`:** `uart_tx_lock` -- fairly coarse-grained

### `uartputc()` -- what does `uart_tx_lock` protect?

1. No races in `uart_tx_buf` operations
2. If queue not empty, UART h/w is executing head of queue
3. No concurrent access to UART write registers

### `uartintr()` -- interrupt handler

- Acquires lock -- might have to wait at interrupt level!
- Removes character from `uart_tx_buf`
- Hands next queued char to UART h/w (2)
- Touches UART h/w registers (3)

## How to implement locks?

### Why not

```c
struct lock { int locked; }
acquire(l) {
  while(1){
    if(l->locked == 0){ // A
      l->locked = 1;    // B
      return;
    }
  }
}
```

- **Oops:** race between lines A and B
- **How can we do A and B atomically?**

### Atomic swap instruction

```assembly
a5 = 1
s1 = &lk->locked
amoswap.w.aq a5, a5, (s1)
```

- **Does this in hardware:**
  - Lock addr globally (other cores cannot use it)
  - temp = *s1
  - *addr = a5
  - a5 = temp
  - Unlock addr
- **RISC-V h/w provides a notion of locking a memory location**
  - Different CPUs have had different implementations
  - Diagram: Cores, bus, RAM, lock thing
  - We are really pushing the problem down to the hardware
  - H/w implements at granularity of cache-line or entire bus
- Memory lock forces concurrent swap to run one at a time, not interleaved

## Look at xv6 spinlock implementation

```c
acquire(l){
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
}
```

- If `lk->locked` was already 1, `sync_lock_test_and_set` sets to 1 (again), returns 1, and the loop continues to spin
- If `lk->locked` was 0, at most one `lock_test_and_set` will see the 0; it will set it to 1 and return 0; other `test_and_set` will return 1
- This is a "spin lock", since waiting cores "spin" in acquire loop

- **What is the `push_off()` about?**
  - Why disable interrupts?

### `release()`

- Sets `lk->locked = 0`
- And re-enables interrupts

## Detail: memory read/write ordering

- Suppose two cores use a lock to guard a counter, x
- And we have a naive lock implementation

### Core A

```c
locked = 1
x = x + 1
locked = 0
```

### Core B

```c
while(locked == 1)
  ...
locked = 1
x = x + 1
locked = 0
```

- **The compiler AND the CPU re-order memory accesses**
  - i.e. they do not obey the source program's order of memory references
  - e.g. the compiler might generate this code for core A:

    ```c
    locked = 1
    locked = 0
    x = x + 1
    ```

    - i.e. move the increment outside the critical section!

- **The legal behaviors are called the "memory model"**

### `release()`'s call to `__sync_synchronize()` prevents re-order

- Compiler won't move a memory reference past a `__sync_synchronize()`
- And (may) issue "memory barrier" instruction to tell the CPU

### `acquire()`'s call to `__sync_synchronize()` has a similar effect

- If you use locks, you don't need to understand the memory ordering rules
- You need them if you want to write exotic "lock-free" code

## Why spin locks?

- **Don't they waste CPU while waiting?**
- **Why not give up the CPU and switch to another process, let it run?**
- **

What if holding thread needs to run; shouldn't waiting thread yield CPU?**

### Spin lock guidelines

- Hold spin locks for very short times
- Don't yield CPU while holding a spin lock

- **Systems provide "blocking" locks for longer critical sections**
  - Waiting threads yield the CPU
  - But overheads are typically higher
  - You'll see some xv6 blocking schemes later

## Advice

- Don't share if you don't have to
- Start with a few coarse-grained locks
- Instrument your code -- which locks are preventing parallelism?
- Use fine-grained locks only as needed for parallel performance
- Use an automated race detector
