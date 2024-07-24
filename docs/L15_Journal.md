# Lecture 15: Linux ext3 Crash Recovery

This lecture: a case study of logging in Linux's ext3 file system with an eye to performance and real-world design details

## Why do we care about logging?

- Crash recovery for complex on-disk file-system data structures
- Good performance, good semantics, not too complex
- Ideas widely used e.g., in databases and distributed systems
  - "log" == "journal"
- Lots to chew on for performance and correctness

## Review: The Problem

### FS on disk, free bitmaps

- FS on disk is a complex data structure -- a tree
- Crash while updating can leave it permanently broken, if not repaired
  - e.g., file append, add block to i-node, mark block non-free
    - If crash w/o repair, that block can be allocated for a 2nd file!
  - e.g., file delete, delete directory entry, mark i-node free
    - If crash w/o repair, dirent still present but i-node marked free, can later be allocated to different file

### Why crashes are threatening

- Crash w/o repair can leave file system permanently broken
- Typically seemingly OK but with mystery problems that will surface later
- The bad pattern: an update that involves writing multiple disk blocks, and crash occurs after some but not all writes

## Review: The Basic Idea of Logging (Both ext3 and xv6)

### Cached blocks, FS on disk, log on disk

#### The Goal

- Make system calls ("operations") atomic w.r.t. crashes, after recovery
- Atomic = all-or-nothing
- System calls initially only write the buffer cache, not disk

#### When system call is done -- commit

1. Write all updated blocks to log on disk
2. Mark on-disk log as committed (i.e., contains all of operation's writes)
3. Later, write blocks to home locations in FS

#### Recovery after crash + reboot

- If log marked as committed, copy blocks from log to home locations

#### Write-ahead rule

- Don't start home FS writes until all writes committed in log on disk
- i.e., don't start modifying FS until *all* the operation's writes are in the log, so the operation can be completely finished if there's a crash.

#### Freeing rule

- Don't erase/overwrite log until all home FS writes are done

## ext3 File System

### Overview

- Ext3 is the modern name of the file system in today's reading
  - For many years, ext3 was the main Linux FS
  - Similar to more recent ext4

### Comparison to xv6

- More concurrency/overlap/asynchrony -- less waiting
- Batching to reduce disk writes
- Ordered data mode:
  - Does not write file content to journal, so only written once
- But: concurrency and ordered data add complexity

### ext3 Data Structures

#### In Memory

- Write-back block cache
- **Current transaction info**:
  - Sequence #
  - Block #s to be logged
  - Outstanding "***handles***" -- one per syscall
- **Prev transaction info**...

#### On Disk

- FS
- **Circular log** -- in a special pre-allocated fixed-size file.

### The ext3 log can hold multiple transactions

| SB: offset+seq # | ... | D4 | ...blocks... | C4 | D5 | ... |
|------------------|------|----|-------------|----|----|-----|

- **Log superblock**: log offset and seq # of ***earliest valid transaction***
  - This is not the FS superblock; it's a block at start of log file
- **Descriptor blocks**: ***magic***, seq, block #s
- **Data blocks** (as described by descriptor)
- **Commit blocks**: ***magic***, seq

### An ext3 transaction is really a "compound" transaction

- Ext3 "***batches***" updated blocks from many system calls into each transaction
- Ext3 commits the **current "open" transaction** every few seconds

### ext3's batching reduces time spent writing the log

1. ***Spreads*** fixed transaction costs over many system calls
   - Fixed costs: descriptor and commit blocks, seek/rotate, syscall barrier
2. "***Write absorption***"
   - Many syscalls in the batch may modify the same block
     - e.g., an app creating many files in the same directory
   - Thus, one disk write for many syscalls' updates

### ext3 also gets performance from two kinds of concurrency

1. ***Multiple system calls*** can add to the current transaction
2. ***Multiple transactions*** may be at various stages:
   - One "open" transaction that's accepting new syscalls
   - Committing transactions doing their log writes
   - Committed transactions writing to home locations in FS on disk

### How does concurrency help performance?

- **CPU concurrency**: many processes can execute FS ops in parallel adding to the current "open" transaction
- **I/O concurrency**: processes can operate on FS in cache while previous transactions are writing the disk, and, a syscall doesn't wait for disk writes to complete (or even start)

### A crash may "forget" the last few seconds of operations

- Even if the system calls completed and returned
- Because system calls return before effects are committed to disk
- Why? To allow batching lots of operations into each transaction.

### Careful applications need to be aware that crash may lose last few ops

- Databases, mail systems, editors, etc.
- Need some combination of careful writing and post-crash recovery but at the application layer, to ensure application invariants
  - e.g., databases layer their own logging on top of FS

### Kernel implementation of ext3 system calls

```c
sys_unlink() {
  h = start()
  get(h, block #) ...
  modify the block(s) in the cache
  stop(h)
}
```

#### `start()`

- Tells the logging system about a new system call
  - Can't commit until all `start()`ed system calls have called `stop()`
- `start()` can block this sys call if needed

#### `get()`

- Tells logging system we'll modify cached block
  - Added to list of blocks to be logged in current transaction

#### `stop()`

- `stop()` does *not* cause a commit
- Transaction can commit iff all included syscalls have called `stop()`

### Committing a transaction to disk

#### (this happens in the background)

1. Block new syscalls
2. Wait for in-progress syscalls to `stop()`
3. Open a new transaction, unblock new syscalls
4. Write descriptor to on-disk log w/ list of block #s
5. Write modified blocks from cache to on-disk log
6. Wait for descriptor and data log disk writes to finish
7. Write the commit record to on-disk log
8. Wait for the commit disk write to finish
9. Now modified blocks allowed to go to homes on disk (but not forced)
   - Though only after all previous transactions have committed too

### When can ext3 re-use log space?

- Log is circular: SB T4 T5 T2 T3
- A transaction's log space can be re-used if:
  - It's the oldest transaction in the log -- T2 in this case
  - T2 has committed
  - T2's blocks have all been written to home locations in FS on disk
    - No longer any need to replay from log after a crash
- Before re-use, must write log superblock with offset/seq of resulting now-oldest transaction in log

### "Freeing" a transaction in the log means noting that the space can be re-used and advancing SB seq/offset over it

- (*Not* putting the blocks in the FS free bitmap).

### What if a crash?

#### What kinds of crashes can logging help with?

- "Fail-stop"
- Followed by reboot
  - e.g., power failure, then power restored
- Crash causes RAM (and thus cached disk blocks) to be lost
- Disk h/w is assumed to be readable after restart
  - And to reflect all *completed* writes up to the time of the crash
  - And perhaps parts of in-progress writes at sector granularity (i.e., no partial sector writes)

### After a crash

- On-disk log may have a bunch of complete transactions, then some partial
- May also have written some of block cache to disk before crash
  - i.e., FS *partially* updated, not safe to use
  - e.g., file i-node has new block, but bit not cleared in free bitmap

### How ext3 recovery works

- SB T8 xT5 T6 T7
  - SB points to T6
  - T8 has overwritten start of T5; T5 previously freed

#### Steps

1. Reboot with intact disk
2. Look in log superblock for offset and seq# of oldest transaction
3. Find the end of the log
   - Scan forward over valid complete transactions
     - Descriptor/commit pairs with correct magic and seq #
   - End of log is transaction before first invalid transaction
4. Re-write all blocks through end of log, in log order
   - Thus completing those partially-written operations

### Write-ahead rule ensures that transactions after end of log cannot have started writing any home locations, so after recovery it will be as if they never happened

#### What if block after last valid commit block looks like a log descriptor?

- i.e., looks like the start of the next transaction?
- Perhaps some file data happens to look like a descriptor?
  - Ext3 prevents this from happening
  - Ext3 replaces magic number in data blocks in journal with zero and sets a flag for that block in the descriptor
- Perhaps a descriptor block left over from an

old freed transaction?

- Seq # will be too low -> end of log

### What if another crash during log replay?

- After reboot, recovery will replay exactly the same log writes

### That was the straightforward part of ext3. There are also a bunch of tricky details

#### Why does ext3 delay start of T2's syscalls until all of T1's syscalls finish?

- i.e., why this:
  - T1: |-syscalls-|
  - T2:             |-syscalls-|
            ---time--->

- This barrier sacrifices some performance
- Example problem that this barrier prevents:
  - T1: |-create(x)-|
  - T2:        |-unlink(y)-|
                 X crash
            ---time--->

- If we allow unlink to start before create finishes:
  - unlink(y) free y's i-node -- i.e., marks it free in block cache
  - create(x) may allocate that same i-node -- i.e., read unlink's write
- If T1 commits, but crash before T2 commits, then:
  - y will exist (the unlink did not commit)
  - x will exist but use the same i-node as y!

- We can't let T1 see T2's updates!
  - So: don't start T2's syscalls until T1's syscalls are done
- (Note ext3's copy-on-write doesn't fix this: it gives T1 a copy of all blocks as of the end of T1, but T2's unlink executed before T1 ended.)
- (Note we can't give T2 its own copies of the blocks, separate from T1's copies, since generally we do want later system calls to see the effects of earlier system calls.)

### Why commit needs to write on-disk log from a snapshot of cached blocks

- Consider this:
  - T1: |--create(d/f)--| ... T1's log writes ...
  - T2:                   |--create(d/g)--|

- Both create()s write d's directory content block
- Suppose T1 writes log to disk *after* T2's create()
- We can't let T1 write T2's update to the log!
  - Then a crash+recovery might replay some but not all of T2
  - e.g., make the directory entry d/g but without initializing g's i-node

#### ext3's solution

- Give T1 a private copy of the block cache as it existed when T1 closed
- T1 commits from this snapshot of the cache
- It's efficient using copy-on-write
- The copies allow syscalls in T2 to proceed while T1 is committing

### So far we've been talking about ext3's "journaled data" mode

- In which file content blocks are written to log as well as meta-data (i-nodes, directory content, free bitmaps)
- So file content is included in atomicity guarantee

### Logging file content is slow, every data block written twice to disk

- Data is usually much bigger than meta-data
- Can we entirely omit file content from the log?
- If we did, when would we write file content to the FS?

#### Can we write file content blocks after committing write() meta-data to log?

- No: if metadata is committed first, crash may leave i-node pointing to blocks from someone else's deleted file, with previous file's content!

### ext3 "ordered data" mode

- Write file content blocks to disk *before* committing log
- (And don't write file content to the log)

#### Thus, for a write()

1. Write file content blocks to FS on disk
2. Wait until content writes finish
3. Commit i-node (w/ block numbers and new size) and block free bitmap

#### If crash after data write, but before commit

- Block on disk has new data
- But not visible, since i-node not updated w/ new block
- No metadata inconsistencies
  - Neither i-node nor free bitmap were updated, so blocks still free

#### Most people use ext3 ordered data mode

- It's much faster: avoids writing data twice

### Why does it make sense for the log to omit file content?

- Goal of the log is to protect FS's own invariants
  - e.g., a block cannot be both free and used in an i-node
- From this point of view, file content is not relevant.
- In any case, the file system doesn't know which groups of application writes need to be atomically written.
- Instead, applications call `fsync()` when they need to wait for data to be safe on the disk.

### Correctness challenges w/ ordered data mode

1. rmdir() frees directory content block, #27
2. File write() (in same compound transaction) allocates block #27
3. write() writes block #27 on disk
4. Crash before rmdir is committed

- After recovery, as if rmdir never happened, but directory content block (#27) has been overwritten!
  - Fix: no re-use of freed block until freeing syscall committed

#### Another example

- rmdir, commit, re-use block in file, ordered file write, commit, crash+recover, replay rmdir
  1. rmdir erases "." and ".." in content block 28, which is logged then frees 28
  2. rmdir commits
  3. write() allocates 28, and writes it (with ordered data mode)
  4. write commits
  5. crash, reboot, log is replayed, including the rmdir's write but write()'s data write was not logged, not replayed

- Result: file is left w/ directory content
  - Fix: add "revoke" log record when freeing dir block, prevents *prior* log records from writing that block.

### Note: both problems due to changing the type of a block (content vs meta-data)

- So another solution might be to never change a block's type

### What if there's not enough free log space for a transaction's blocks?

- Free oldest transaction(s)
  - i.e., install its writes in the on-disk FS

### What if so many syscalls have started that the entire log isn't big enough to hold their writes, even if all older transactions have been freed?

- Syscall passes max number of blocks needed to start()
- Start() waits if total for current transaction is too high and works on freeing old transactions

### Another (complex) reason for reservations

- T1 updates block 17
- T1 commits
- T1 finishes writing its log and commit record but has not yet written block 17 to home location
- T2 also updates block 17 in the cache
- A T2 syscall is executing, does a write for which no log space
- Can ext3 install T1's blocks to home locations on disk, and free T1's log space?
- No: the cache no longer holds T1's version of block 17 (ext3 could read the block from the log, but it doesn't)
- But once T2 commits, logged block 17 will reflect T1's writes so freeing of T1 can just wait for T2 to commit
- That is, freeing of early transactions may have to wait for commit of later transactions

### This means any transaction (T2) *must* be able to commit without having to wait for freeing of earlier transaction (T1)

- Log space reservations help ensure this

### Checksums

- Recall: transaction's log blocks must be on disk before writing commit block
  - ext3 waits for disk to say "done" before starting commit block write

#### Risk

- Disks usually have write caches and re-order writes, for performance
  - Sometimes hard to turn off (the disk lies)
  - People often leave re-ordering enabled for speed, out of ignorance
  - Bad news if disk writes commit block before the rest of the transaction

#### Solution

- Commit block contains checksum of entire logged transaction
  - On recovery: compute checksum of transaction
    - If matches checksum in commit block: install transaction
    - If no match: don't install transaction
- Ext4 has log checksumming, but ext3 does not

## Next Week

- Creative uses of virtual memory at application level

## References

- Stephen Tweedie 2000 talk transcript "EXT3, Journaling Filesystem"
  - [EXT3, Journaling Filesystem](http://olstrans.sourceforge.net/release/OLS2000-ext3/OLS2000-ext3.html)
- Log format details
  - [Ext4 Disk Layout](https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout)
- [USENIX article](https://www.usenix.org/system/files/login/articles/04_tso_018-021_final.pdf)
