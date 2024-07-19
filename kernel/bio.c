// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// The code referred to the implementation from [https://blog.miigon.net/posts/s081-lab8-locks/#buffer-cache-hard].

// ===================== Lab8: Buffer Cache =====================:
#define BUCKET_SIZE 13
#define BUFFERMAP_HASH(dev, blockno) ((((dev) << 27) | (blockno)) % BUCKET_SIZE)
// :===================== Lab8: Buffer Cache =====================

struct {
  // ===================== Lab8: Buffer Cache =====================:
  // struct spinlock lock;   // commented out for Lab8
  
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;  // commented out for Lab8

  struct buf blockBufferMap[BUCKET_SIZE];    // hash map from dev and blockno to buf
  struct spinlock bucketLocks[BUCKET_SIZE];  // locks for each bucket
  // :===================== Lab8: Buffer Cache =====================
} bcache;

// ===================== Lab8: Buffer Cache =====================:
static inline void
bufferMap_InsertBucket(uint key, struct buf *b)
{
  b->next = bcache.blockBufferMap[key].next;
  bcache.blockBufferMap[key].next = b;
}

/**
Look for block on device dev inside a specific buffer map bucket.
If found, return buffer, otherwise return null.
Must already be holding buffermap_lock[key] for this to be thread-safe
*/
static inline struct buf*
bufferMap_SearchBucket(uint key, uint dev, uint blockno)
{
  struct buf *b;

  for (b = bcache.blockBufferMap[key].next; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno && b->trash == 0) {
      return b;
    }
  }

  return (struct buf*)0;
}
// :===================== Lab8: Buffer Cache =====================

void
binit(void)
{
  // ===================== Lab8: Buffer Cache =====================:
  // commented out for Lab8
  /*
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  */

  for (int i = 0; i < BUCKET_SIZE; i++) {  // initialize blockBufferMap
    initlock(&bcache.bucketLocks[i], "bcache_bufmap");
    // the first buf in linked list is a dummy node
    bcache.blockBufferMap[i].next = (struct buf*)0;
  }

  for (int i = 0; i < NBUF; i++) {  // initialize buffers
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->valid = 0;
    b->trash = 1;  // needs to be evicted and rehashed before use
    b->lastUseTimestamp = 0;
    b->refcnt = 0;
    
    // spread all the buffers among blockBufferMap buckets evenly
    bufferMap_InsertBucket(i % BUCKET_SIZE, b);
  }

  // :===================== Lab8: Buffer Cache =====================
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // ===================== Lab8: Buffer Cache =====================:
  // commented out for Lab8
  /*
  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
  */

  uint key = BUFFERMAP_HASH(dev, blockno);

  acquire(&bcache.bucketLocks[key]);

  // Is the block already cached?
  if ((b = bufferMap_SearchBucket(key, dev, blockno))) {
    b->refcnt++;
    release(&bcache.bucketLocks[key]);
    acquiresleep(&b->lock);
    return b;
  }

  // If the block is not cached

  /*
  To get a suitable block to reuse, we need to search for one in all the buckets,
  which means acquiring their bucket locks.
  but it's not safe to try to acquire every single bucket lock while holding one.
  it can easily lead to circular wait, which produces deadlock.
   */
  release(&bcache.bucketLocks[key]);

  /*
  We need to release our bucket lock so that iterating through all the buckets won't
  lead to circular wait and deadlock. however, as a side effect of releasing our bucket
  lock, other CPUs might request the same blockno at the same time and the cache buf for  
  blockno might be created multiple times in the worst case. since multiple concurrent
  bget requests might pass the "Is the block already cached?" test and start the 
  eviction & reuse process concurrently for the same blockno.
   */

  // Eviction Process
  /*
  The eviction process consists of two phases:
  1. stealing: search for an available buf in all buckets, steal(evict) it for our new block to use;
  2. inserting: insert the newly evicted buf into blockno's bucket.
  */

  // Find the one LRU buf among all buckets,
  // finish with it's corresponding bucket's lock held.
  struct buf *LRU_Prev = (struct buf*)0;
  uint holdingBucket = -1;

  for (int i = 0; i < BUCKET_SIZE; i++) {
    // Before acquiring, we are either holding nothing, or only locks of
    // buckets that are on the left side of the current bucket
    // so no circular wait can never happen here. (safe from deadlock)
    acquire(&bcache.bucketLocks[i]);
    int newFound = 0; // if the new least-recently-used buf is found in this bucket
    
    for (b = &bcache.blockBufferMap[i]; b->next; b = b->next) {
      if ((b->trash || b->next->refcnt == 0) && (!LRU_Prev || b->next->lastUseTimestamp < LRU_Prev->next->lastUseTimestamp)) {
        LRU_Prev = b;
        newFound = 1;
      }
    }

    if (!newFound) {
      release(&bcache.bucketLocks[i]);
    } else {
      if (holdingBucket != -1) {
        release(&bcache.bucketLocks[holdingBucket]);
      }
      
      holdingBucket = i;
      // keep holding this bucket's lock....
    }
  }

  if (!LRU_Prev) {
    panic("bget: no buffers");
  }

  struct buf* newBuffer = LRU_Prev->next;

  if (holdingBucket != key) {
    // remove the buf from it's original bucket.
    LRU_Prev->next = newBuffer->next;
    release(&bcache.bucketLocks[holdingBucket]);

    // re-acquire blockno's bucket lock, for later insertion.
    acquire(&bcache.bucketLocks[key]);
  }

  // stealing phase end, inserting phase start

  /*
  We have to check again: is this blockno now cached by another process?
  we need to do this because during the stealing phase we don't hold
  the bucket lock for blockBufferMap[key] to prevent circular wait, but this can
  lead to duplicate concurrent cache allocation for blockno. 
   */
  if((b = bufferMap_SearchBucket(key, dev, blockno))) {
    b->refcnt++;

    if(holdingBucket != key) {
      // still insert newBuffer into blockBufferMap[key], but as a trash buffer.
      // (do not return to original bucket, to prevent deadlock)
      // trash buffers will not be accessed before being evicted and re-hashed (un-trashed)
      newBuffer->trash = 1;
      newBuffer->lastUseTimestamp = 0; // so it will be evicted and re-used earlier.

      bufferMap_InsertBucket(key, newBuffer);
    } else {
      // don't need to trash it because we haven't removed it from it's original bucket
      // and haven't done anything to alter it in any way.
    }

    release(&bcache.bucketLocks[key]);
    acquiresleep(&b->lock);
    
    return b;
  }

  // If still doesn't exist, now insert newBuffer into bcache.blockBufferMap[key]

  if(holdingBucket != key) {
    // should already been holding &bcache.bucketLocks[key],
    // rehash and add it to the correct bucket
    bufferMap_InsertBucket(key, newBuffer);
  }

  // configure newBuffer and return
  newBuffer->trash = 0; // un-trash
  newBuffer->dev = dev;
  newBuffer->blockno = blockno;
  newBuffer->refcnt = 1;
  newBuffer->valid = 0;
  release(&bcache.bucketLocks[key]);
  acquiresleep(&newBuffer->lock);
  
  return newBuffer;
  // :===================== Lab8: Buffer Cache =====================
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // ===================== Lab8: Buffer Cache =====================:
  // commented out for Lab8
  /*
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
  */

  uint key = BUFFERMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bucketLocks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastUseTimestamp = ticks;  // from trap.c
  }
  release(&bcache.bucketLocks[key]);
  // :===================== Lab8: Buffer Cache =====================
}

void
bpin(struct buf *b) {
  // ===================== Lab8: Buffer Cache =====================:
  uint key = BUFFERMAP_HASH(b->dev, b->blockno);

  acquire(&bcache.bucketLocks[key]);
  b->refcnt++;
  release(&bcache.bucketLocks[key]);
  // :===================== Lab8: Buffer Cache =====================
}

void
bunpin(struct buf *b) {
  // ===================== Lab8: Buffer Cache =====================:
  uint key = BUFFERMAP_HASH(b->dev, b->blockno);
  
  acquire(&bcache.bucketLocks[key]);
  b->refcnt--;
  release(&bcache.bucketLocks[key]);
  // :===================== Lab8: Buffer Cache =====================
}


