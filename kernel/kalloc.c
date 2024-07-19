// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// ===================== Lab8: Locks =====================:
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
// :===================== Lab8: Locks =====================

void
kinit()
{
  // ===================== Lab8: Locks =====================:
  char lockName[10];
  for (int i = 0; i < NCPU; i++) {
    snprintf(lockName, sizeof(lockName), "kmem_cpu%d", i);
    initlock(&kmem[i].lock, lockName);
  }
  // :===================== Lab8: Locks =====================
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // ===================== Lab8: Locks =====================:
  push_off();
  int cpuID = cpuid();
  pop_off();
  
  acquire(&kmem[cpuID].lock);
  r->next = kmem[cpuID].freelist;
  kmem[cpuID].freelist = r;
  release(&kmem[cpuID].lock);
  // :===================== Lab8: Locks =====================
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // ===================== Lab8: Locks =====================:
  push_off();
  int cpuID = cpuid();
  pop_off();

  acquire(&kmem[cpuID].lock);
  
  r = kmem[cpuID].freelist;
  if(r) {  // normal case
    kmem[cpuID].freelist = r->next;
  } else {  // "steal" page from another CPU
    int id;
    for (id = 0; id < NCPU; id++) {
      if (id == cpuID) {
        continue;
      }

      acquire(&kmem[id].lock);

      r = kmem[id].freelist;
      if (r) {
        kmem[id].freelist = r->next;
        release(&kmem[id].lock);
        break;
      }

      release(&kmem[id].lock);
    }
  }
  
  release(&kmem[cpuID].lock);
  // :===================== Lab8: Locks =====================

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
