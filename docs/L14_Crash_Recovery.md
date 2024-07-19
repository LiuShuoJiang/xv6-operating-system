# Lecture 15: Crash Recovery, Logging

## Plan

**Problem:** Crash recovery

- Crash leads to inconsistent on-disk file system

**Solution:** Write-ahead logging

## Why Crash Recovery

### What is Crash Recovery?

- You're writing the file system, then the power fails
- You reboot, is your file system still usable?

### The Problem

- Crash during multi-step operation may leave FS invariants violated
- After reboot:
  - Bad: crash again due to corrupt FS
  - Worse: no crash, but reads/writes incorrect data

### Examples

#### Create

```bash
$ echo hi > x
```

Trace from last lecture:

- `bwrite: block 33 by ialloc` (allocate inode in inode block 33)
- `bwrite: block 33 by iupdate` (update inode, e.g., set nlink)
- `bwrite: block 46 by writei` (write directory entry, adding "x" by dirlink())
- `bwrite: block 32 by iupdate` (update directory inode, because inode may have changed)

**Crash Scenarios:**

- Crash between `iupdate` and `writei`: Allocate file inode, crash: inode not free but not used -- not so bad
- If the file system first wrote 46 and 32 and then 33: If crash between 32 and 33, then dirent points to free inode -- disaster!

#### Write

- Inode `addrs[]` and `len`
- Indirect block
- Block content
- Block free bitmap
- Crash: inode refers to free block -- disaster!
- Crash: block not free but not used -- not so bad

#### Unlink

- Block free bitmaps
- Free inode
- Erase dirent

### What Can We Hope For?

After rebooting and running recovery code:

1. FS internal invariants maintained
   - e.g., no block is both in free list and in a file
2. All but last few operations preserved on disk
   - e.g., data I wrote yesterday are preserved but perhaps not data I was writing at time of crash
   - User might have to check last few operations
3. No order anomalies
   - `echo 99 > result ; echo done > status`

### Correctness and Performance Often Conflict

- Disk writes are slow!
  - Safety: write to disk ASAP
  - Speed: don't write the disk (batch, write-back cache, sort by track, etc.)

### Crash Recovery is a Recurring Problem

- Arises in all storage systems, e.g., databases
- Many clever performance/correctness trade-offs

## Logging Solution

### Most Popular Solution: Logging (== Journaling)

- Goal: Atomic system calls w.r.t. crashes
- Goal: Fast recovery (no hour-long fsck)
- Will introduce logging in two steps:
  - First xv6's log, which only provides safety and fast recovery
  - Then Linux EXT3, which is also fast in normal operation

### The Basic Idea Behind Logging

- You want atomicity: all of a system call's writes, or none
  - Let's call an atomic operation a "transaction"
- Record all writes the sys call *will* do in the log on disk (log)
- Then record "done" on disk (commit)
- Then do the FS disk writes (install)
- On crash+recovery:
  - If "done" in log, replay all writes in log
  - If no "done", ignore log

**This is a WRITE-AHEAD LOG**.

### Write-Ahead Log Rule

- Install *none* of a transaction's writes to disk until *all* writes are in the log on disk, and the logged writes are marked committed.

### Why the Rule?

- Once we've installed one write to the on-disk FS, we have to do *all* of the transaction's other writes -- so the transaction is atomic.
- We have to be prepared for a crash after the first installation write, so the other writes must be still available after the crash -- in the log.

### Logging is Magic

- Crash recovery of complex mutable data structures is generally hard
- Logging can often be layered on existing storage systems
- It's compatible with high performance (topic for next week)

## Overview of xv6 Logging

### xv6 Log Representation

```
[Diagram: buffer cache, in-memory log block # array, FS tree on disk, log header and blocks on disk]
```

- On write, add block number to in-memory array
- Keep the data itself in buffer cache (pinned)
- On commit:
  - Write buffers to the log on disk
  - WAIT for disk to complete the writes ("synchronous")
  - Write the log header sector to disk
    - Block numbers
    - Non-zero "n"
- After commit:
  - Install (write) the blocks in the log to their home location in FS
  - Unpin blocks
  - Write zero to "n" in the log header on disk

### The "n" Value in the Log Header on Disk Indicates Commit

- Non-zero == committed, log content valid and is a complete transaction
- Zero == not committed, may not be complete, recovery should ignore log
- Write of non-zero "n" is the "commit point"

### xv6 Disk Layout with Block Numbers

| Block Number | Description          |
|--------------|----------------------|
| 2            | Log head             |
| 3            | Logged blocks        |
| 32           | Inodes               |
| 45           | Bitmap               |
| 46           | Content blocks       |

### Example

```bash
$ echo a > x
```

**Create:**

- `bwrite 3` (inode, 33)
- `bwrite 4` (directory content, 46)
- `bwrite 5` (directory inode, 32)
- `bwrite 2` (commit, block #s and n)
- `bwrite 33` (install inode for x)
- `bwrite 46` (install directory content)
- `bwrite 32` (install dir inode)
- `bwrite 2` (mark log "empty")

**Write:**

- `bwrite 3`
- `bwrite 4`
- `bwrite 5`
- `bwrite 2`
- `bwrite 45` (bitmap)
- `bwrite 595` (a)
- `bwrite 33` (inode, file size)
- `bwrite 2`

**Write:**

- `bwrite 3`
- `bwrite 4`
- `bwrite 2`
- `bwrite 595` (\n)
- `bwrite 33` (inode)
- `bwrite 2`

### Let's Look at the 2nd Transaction, a write()

- First file.c:syswrite:
  - Compute how many blocks we can write before log is full
  - Write that many blocks in a transaction

- Combined with fs.c:writei:
  - `begin_op()`
    - `bmap()` -- can write bitmap, indirect block
      - `log_write` to `bzero` new block
    - `bread()`
    - Modify `bp->data`
    - `log_write()`
      - Absorbs `bzero`
    - `iupdate()` -- writes inode
  - `end_op()`

### begin_op() in log.c

- Need to indicate which group of writes must be atomic!
- Need to check if log is being committed
- Need to check if our writes will fit in remainder of log

### log_write()

- Add sector # to in-memory array
- `bpin()` will pin block in buffer cache, so that bio.c won't evict it

### end_op()

- If no outstanding operations, commit

### commit()

- Copy updated blocks from cache to log on disk
- Record sector #s and "done" in on-disk log header
- Install writes -- copy from on-disk log to on-disk FS
  - `bunpin()` will unpin from cache -- now it can be evicted
- Erase "done" from log

### What Would Have Happened if We Crashed During a Transaction?

- Memory is lost, leaving only the disk as of the crash
- Kernel calls `recover_from_log()` during boot, before using FS
  - If log header block says "done":
    - Copy blocks from log to real locations on disk
- What is in the on-disk log?
  - Crash before commit
  - Crash during commit -- commit point?
  - Crash during install_trans()
  - Crash just after reboot, while in recover_from_log()
- Note: it is OK to replay the log more than once!
  - As long no other activity intervenes

### Note xv6 Assumes the Disk is Fail-Stop

- It either does the write correctly, or does not do the write
  - Perhaps it can't do the last write due to power failure
- Thus:
  - No partial writes (each sector write is atomic)
  - No wild writes
  - No decay of sectors (no read errors)
  - No read of the wrong sector

## Challenges

### Challenge: Prevent Write-Back from Cache

- A system call can safely update a *cached* block, but the block cannot be written to the FS until the transaction commits
- Tricky because, e.g., cache may run out of space, and be tempted to evict some entries in order to read and cache other data.

#### Consider Create Example

- Write dirty inode to log
- Write dir block to log
- Evict dirty inode
- Commit

#### xv6 Solution 1

- Ensure buffer cache is big enough
- Pin dirty blocks in buffer cache
- After commit, unpin block

### Challenge: System's Call Data Must Fit in Log

#### xv6 Solution 2

- Compute an upper bound of number of blocks each system call writes, set log size >= upper bound
- Break up some system calls into several transactions, for example, large `write()`s
  - Thus: large `write()`s are not atomic, but a crash will leave a correct prefix of the write

### Challenge: Allowing Concurrent System Calls

- Must allow writes from several calls to be in log
- On commit must write them all
- BUT cannot write data from calls still in a transaction

#### xv6 Solution 3

- Allow no new system calls to start if their data might not fit in log
  - Must wait for current calls to complete and leave enough space in log
  - Or, wait until concurrent calls have committed, which frees up log
- When number of in-progress calls falls to zero
  - Commit
  - Free up log space
  - Wake up waiting calls

### Challenge: A Block May Be Written Multiple Times in a Transaction

- `log_write()` affects only the cached block in memory
- So a cached block may reflect multiple uncommitted transactions
- But install only happens when there are no in-progress transactions, so installed blocks reflect only committed transactions
- Good for performance: "write absorption"

## Summary

### What is Good About xv6's Log Design?

- Correctness due to write-ahead log
- Good disk throughput: log naturally batches writes
  - But data disk blocks are written twice
- Concurrency

### What's Wrong with xv6's Logging?

- Not very efficient:
  - Every block is written twice (log and install)
  - Logs whole blocks even if only a few bytes modified
  - Writes each log block synchronously
    - Could write them as a batch and only write head synchronously
  - Log writes and install writes are eager
    - Both could be lazy, for more write absorption, but must still write the log first
- Trouble with operations that don't fit in the log
  - `unlink` might dirty many blocks while truncating file

