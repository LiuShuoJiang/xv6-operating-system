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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// ===================== Lab5: Copy-on-Write Fork =====================:
// reference count for each page
struct reference_struct {
  int referenceCount[PHYSTOP / PGSIZE];
  struct spinlock lock;
} reference;
// :===================== Lab5: Copy-on-Write Fork =====================

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // ===================== Lab5: Copy-on-Write Fork =====================:
  initlock(&reference.lock, "reference");
  // :===================== Lab5: Copy-on-Write Fork =====================
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // ===================== Lab5: Copy-on-Write Fork =====================:
    acquire(&reference.lock);
    // Refcount will be decremented by 1 in kfree
    // We have to set it to 1 first, otherwise it will be decremented to a negative number
    reference.referenceCount[(uint64)p / PGSIZE] = 1;
    release(&reference.lock);
    // :===================== Lab5: Copy-on-Write Fork =====================
    kfree(p);
  }
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

  // ===================== Lab5: Copy-on-Write Fork =====================:
  // only free memory when refcount is 0, otherwise decrease refcount by 1
  acquire(&reference.lock);
  if (--reference.referenceCount[(uint64)pa / PGSIZE] == 0) {
    release(&reference.lock);

    // ========== original xv6 code ==========:
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
    // :========== original xv6 code ==========
  } else {
    release(&reference.lock);
  }
  // :===================== Lab5: Copy-on-Write Fork =====================
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    // ===================== Lab5: Copy-on-Write Fork =====================:
    acquire(&reference.lock);
    reference.referenceCount[(uint64)r / PGSIZE] = 1;
    release(&reference.lock);
    // :===================== Lab5: Copy-on-Write Fork =====================
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// =========================== Lab5: Copy-on-Write Fork ===========================:
// get the reference count of a page
int kGetReferenceCount(uint64 pa)
{
  return reference.referenceCount[pa / PGSIZE];
}

// increase the reference count of a page
int kIncreaseReferenceCount(uint64 pa)
{
  // check if the address is valid
  if (pa % PGSIZE != 0 || (char*)pa < end || pa >= PHYSTOP) {
    return -1;
  }
  
  acquire(&reference.lock);
  ++reference.referenceCount[pa / PGSIZE];
  release(&reference.lock);

  return 0;
}

// check if the page is a copy-on-write user page, used in trap.c
int checkCopyOnWritePage(pagetable_t pagetable, uint64 va)
{
  if (va >= MAXVA) {
    return -1;
  }

  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return -1;
  }

  if ((*pte & PTE_V) == 0 || (*pte & PTE_COW) == 0 || 
    (*pte & PTE_U) == 0 || (*pte & PTE_PREV_WRITE) == 0) {
    return -1;
  }

  return 0;
}

// allocate a new page for the copy-on-write page, used in trap.c and vm.c
int allocCopyOnWritePage(pagetable_t pagetable, uint64 va)
{
  if (va % PGSIZE != 0) {
    return -1;
  }

  uint64 pa = walkaddr(pagetable, va);
  if (pa == 0) {
    return -1;
  }

  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return -1;
  }

  // redundant code for user testing
  // if (kGetReferenceCount(pa) == 1) {
  //   *pte |= PTE_W;
  //   *pte &= ~PTE_COW;
  //   *pte &= ~PTE_PREV_WRITE;
  //   return 0;
  // }
  
  char *mem = kalloc();  // allocate a new page
  if (mem == 0) {
    return -1;
  }

  // these flags should be retrieved before uvunmap!!!
  uint flags = PTE_FLAGS(*pte);  // get the flags of the old page
  flags &= (~PTE_COW); // clear the CoW flag
  flags &= (~PTE_PREV_WRITE);  // clear the PREV_WRITE flag
  flags |= PTE_W;  // set the writable flag

  memmove(mem, (char*)pa, PGSIZE); // copy the content of the old page to the new page

  uvmunmap(pagetable, va, 1, 1);  // unmap the old page

  // map the new page to the same virtual address
  if (mappages(pagetable, va, PGSIZE, (uint64)mem, flags) != 0) {
    kfree(mem);
    return -1;
  }
  
  return 0;
}
// :=========================== Lab5: Copy-on-Write Fork ===========================
