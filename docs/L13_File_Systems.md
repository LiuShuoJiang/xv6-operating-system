# Lecture 14: File System

## Why are file systems useful?

- **Durability** across restarts
- **Naming** and organization
- **Sharing** among programs and users

## Why interesting?

- ***Crash recovery***
- ***Performance*** /concurrency
- Sharing
- Security
- **Abstraction is useful**: pipes, devices, /proc, /afs, Plan 9
  - So FS-oriented apps work with many kinds of objects
- Topic of in two labs

## API example -- UNIX / POSIX / Linux / xv6/&c

```c
fd = open("x/y", -);
write(fd, "abc", 3);
link("x/y", "x/z");
unlink("x/y");
write(fd, "def", 3);  // offset is implicit
close(fd);
// file y/z contains abcdef
```

### High-level choices visible in the UNIX FS API

- Objects: files (vs virtual disk, DB)
- Content: ***byte array*** (vs 80-byte records, *BTree* )
- Naming: human-readable (vs object IDs)
- Organization: name hierarchy
- Synchronization: none (vs locking, versions)
  - `link()/unlink()` can change name hierarchy concurrently with an `open()`
- There are other file system APIs, sometimes quite different!

### A few implications of the API

- `fd` refers to something
  - that is preserved even if file name changes
  - or if file is deleted while open!
- A file can have multiple links
  - i.e., occur in multiple directories
  - no one of those occurrences is special
  - so *file must have info stored somewhere other than directory*
- Thus:
  - FS records file info in an "**inode**" on disk
  - FS refers to inode with **i-number** (internal version of FD)
  - Inode must have ***link count*** (tells us when to free)
  - Inode must have ***count of open FDs***
  - Inode deallocation *deferred* until last link and FD are gone

## Let's talk about xv6

### FS software layers

- **System calls**
- **Name ops | FD ops**
- **Inodes**
- **Inode cache**
- **Log**
- **Buffer cache**
- **Virtio disk driver**

### Data stored on a persistent medium

- Data stays on disk without power
- Common storage medium:
  - **Hard disk drives** (big but slow, inexpensive)
  - **Solid state drives** (smaller, but fast, and more expensive)
- Historically, disks were read/write usually in 512-byte units, called **sectors**

#### Hard Disk Drives (HDD)

- Concentric tracks
- Each track is a sequence of sectors
- Head must seek, disk must rotate
  - Random access is slow (5 or 10ms per access)
  - Sequential access is much faster (100 MB/second)
- ECC on each sector
- Can only read/write whole sectors
- Thus: sub-sector writes are expensive (read-modify-write)

#### Solid State Drives (SSD)

- Non-volatile "flash" memory
- Random access: 100 microseconds
- Sequential: 500 MB/second
- Internally complex -- hidden except sometimes performance
  - Flash must be erased before it's re-written
  - Limit to the number of times a flash block can be written
  - SSD copes with a level of indirection -- remapped blocks

For both HDD and SSD:

- Sequential access is much faster than random
- Big reads/writes are faster than small ones
- Both of these influence FS design and performance

#### Disk Blocks

- Most o/s use blocks of multiple sectors, e.g. 4 KB blocks = 8 sectors
- To reduce book-keeping and seek overheads
- Xv6 uses 2-sector blocks

#### On-disk layout

- Xv6 treats disk as ***an array of sectors*** (ignoring physical properties of disk)
- `0`: **unused**
- `1`: **super block** (size, ninodes)
- `2`: **log for transactions**
- `32`: **array of inodes**, packed into blocks
- `45`: **block in-use bitmap** (`0`=free, `1`=used)
- `46`: **file/dir content blocks**
- End of disk

#### Xv6's `mkfs` program generates this layout for an empty file system

- The layout is static for the file system's lifetime
- See output of `mkfs`

#### "Meta-data"

- Everything on disk other than file content
- Super block, i-nodes, bitmap, directory content

#### On-disk inode

- Type (free, file, directory, device)
- nlink
- size
- addrs\[12+1\]

#### Direct and indirect blocks

### Example

- How to find file's byte 8000?
- Logical block 7 = 8000 / BSIZE (=1024)
- 7th entry in `addrs`

### Each i-node has an i-number

- Easy to turn i-number into inode
- Inode is 64 bytes long
- Byte address on disk: `32*BSIZE + 64*inum`

### Directory contents

- Directory much like a file
  - But user can't directly write
- Content is array of dirents
- Dirent:
  - inum
  - 14-byte file name
- Dirent is free if inum is zero

### You should view FS as an on-disk data structure

- [tree: dirs, inodes, blocks]
- With two allocation pools: inodes and blocks

### Let's look at xv6 in action

- Focus on disk writes
- Illustrate on-disk data structures via how updated

#### Q: How does xv6 create a file?

```sh
rm fs.img & make qemu

$ echo hi > x
// create
bwrite: block 33 by ialloc   // allocate inode in inode block 33
bwrite: block 33 by iupdate  // update inode (e.g., set nlink)
bwrite: block 46 by writei   // write directory entry, adding "x" by dirlink()
bwrite: block 32 by iupdate  // update directory inode, because inode may have changed
bwrite: block 33 by iupdate  // itrunc new inode (even though nothing changed)
// write
bwrite: block 45 by balloc   // allocate a block in bitmap block 45
bwrite: block 745 by bzero   // zero the allocated block (block 745)
bwrite: block 745 by writei  // write to it (hi)
bwrite: block 33 by iupdate  // update inode
// write
bwrite: block 745 by writei  // write to it (\n)
bwrite: block 33 by iupdate  // update inode
```

#### Call graph 1

```plaintext
sys_open        sysfile.c
  create        sysfile.c
    ialloc      fs.c
    iupdate     fs.c
    dirlink     fs.c
      writei    fs.c
      iupdate fs.c
  itrunc        sysfile.c
    iupdate     fs.c
```

#### Q: What's in block 33?

- Look at create() in sysfile.c

#### Q: Why *two* writes to block 33?

#### Q: What is in block 32?

#### Q: How does xv6 write data to a file? (see write part above)

#### Call graph 2

```plaintext
sys_write       sysfile.c
  filewrite     file.c
    writei      fs.c
      bmap
        balloc
          bzero
      iupdate
```

#### Q: What's in block 45?

- Look at writei call to bmap
- Look at bmap call to balloc

#### Q: What's in block 745?

#### Q: Why the iupdate?

- File length and addrs[]

#### Q: How does xv6 delete a file?

```sh
$ rm x
bwrite: block 46 by writei    // from sys_unlink; directory content
bwrite: block 32 by iupdate   // from writei of directory content
bwrite: block 33 by iupdate   // from sys_unlink; link count of file
bwrite: block 45 by bfree     // from itrunc, from iput
bwrite: block 33 by iupdate   // from itrunc; zeroed length
bwrite: block 33 by iupdate   // from iput; marked free
```

#### Call graph 3

```plaintext
sys_unlink
  writei
  iupdate
  iunlockput
    iput
      itrunc
        bfree
        iupdate
      iupdate
```

#### Q: What's in block 46?

- Sys_unlink in sysfile.c

#### Q: What is in block 33?

#### Q: What's in the block 45?

- Look at iput

#### Q: Why four `iupdates`?

### Concurrency in file system

- Xv6 has modest goals
  - Parallel read/write of different files
  - Parallel pathname lookup
- But, even those poses interesting correctness challenges

#### E.g., what if there are concurrent calls to `ialloc`?

- Will they get the same inode?
- Note `bread` / `write` / `brelse` in `ialloc`
  - `Bread` locks the block, perhaps waiting, and reads from disk
  - `Brelse` unlocks the block

### Let's look at the block cache in `bio.c`

- `Block` cache holds just a few recently-used blocks
- `Bcache` at start of `bio.c`

#### FS calls `bread`, which

calls `bget`

- `Bget` looks to see if block already cached
  - If present, lock (may wait) and return the block
    - May wait in `sleeplock` until current using processes releases
    - Sleep lock puts caller to sleep, if already locked
  - If not present, re-use an existing buffer
  - `B->refcnt++` prevents buf from being recycled while we're waiting
  - Invariant: **one copy of a disk block in memory**

### Two levels of locking here

- `Bcache.lock` protects the description of what's in the cache
- `B->lock` protects just the one buffer

#### Q: What is the block cache replacement policy?

- Prev ... head ... next
- `Bget` re-uses `bcache.head.prev` -- the "tail"
- `Brelse` moves block to `bcache.head.next`

#### Q: Is that the best replacement policy?

#### Q: Why does it make sense to have a double copy of I/O?

- Disk to buffer cache
- Buffer cache to user space
- Can we fix it to get better performance?

#### Q: How much RAM should we dedicate to disk buffers?

### Pathname Lookup

- Traverse a pathname element at the time
- Potentially many blocks involved:
  - Inode of top directory
  - Data of top directory
  - Inode of next-level down
  - .. and so on ..
- Each one of them might result in a cache miss
  - Disk access are expensive
- Allow parallel pathname lookup
  - If one process blocks on disk, another process may proceed with lookup
  - Challenging: unlink may happen concurrent with lookup

### Let's look at `namex()` (`kernel/fs.c`)

- `Ilock()`: locks current directory
- Find next directory inode
- Then unlock current directory
- Another process may unlink the next inode
  - But inode won't be deleted, because inode's `refcnt > 0`
- Risk: next points to same inode as current (lookup of ".")
  - Unlock current before getting lock on next
  - Key idea: getting a reference separately from locking

