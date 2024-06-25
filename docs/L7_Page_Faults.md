# Lecture 7: Page Faults

## Plan: Cool Things You Can Do with Virtual Memory

- **Better Performance/Efficiency**
  - Example: one zero-filled page
  - Example: copy-on-write fork
- **New Features**
  - Example: memory-mapped files

## Virtual Memory: Several Views

- **Primary Purpose: Isolation**
  - Each process has its own address space
- **Virtual Memory Provides a Level-of-Indirection**
  - Provides kernel with opportunities to do cool stuff
  - Already some examples:
    - Shared trampoline page
    - Guard page
  - More possibilities...

## Key Idea: Change Page Tables on Page Fault

- Page fault is a form of a trap (like a system call)
- Xv6 panics on page fault
  - But you don't have to panic!
- Instead:
  - Update page table instead of panic
  - Restart instruction (see `userret()` from traps lecture)
  - Combination of page faults and updating page table is powerful!

## RISC-V Page Faults

- 3 of 16 exceptions are related to paging
- Exceptions cause controlled transfers to kernel
  - See traps lecture

## Information Needed at Page Fault to Do Something Interesting

1. The virtual address that caused the fault
   - See `stval` register; page faults set it to the ***fault address***
2. The type of violation that caused the fault
   - See `scause` register value (instruction, load, and store page fault)
3. The instruction and mode ***where the fault occurred***
   - User IP: `tf->epc`
   - U/K mode: implicit in `usertrap`/`kerneltrap`

## Lazy/On-Demand Page Allocation

- **`sbrk()` is old fashioned**
  - Applications often ask for memory they need
  - For example, they allocate for the largest possible input but use less
  - If they ask for much, `sbrk()` could be expensive
  - `sbrk` allocates memory that may never be used
- **Modern OSes Allocate Memory Lazily**
  - Plan:
    - Allocate physical memory when application needs it
    - Adjust `p->sz` on `sbrk`, but don't allocate
    - When application uses that memory, it will result in a page fault
    - ***On page fault, allocate memory***
    - Resume at the fault instruction
  - May use less memory
    - If not used, no fault, no allocation
  - Spreads the cost of allocation over the page faults instead of upfront in `sbrk()`
- **Demo**
  - Modify `sysproc.c`
    - Run `ls` and panic
    - Look at `sh.c`
  - Modify `trap.c`
  - Modify `vm.c`
    - `vmfault`, `ismapped`, `uvmunmap`, `copyin`, `copyout`, `uvmcopy`
  - Run `top`

## One Zero-Filled Page (Zero Fill on Demand)

- Applications often have large parts of memory that must be zero
  - Global arrays, etc.
  - The "block starting symbol" (bss) segment
- Thus, kernel must often fill a page with zeros
- **Idea**: `memset` ***one*** read-only page with zeros
  - Map that page copy-on-write when kernel needs zero-filled page
  - On write (we get page fault), make a copy of the page and map it read/write in app address space

## Copy-On-Write Fork

- **Observation**: xv6 fork copies all pages from parent (see `fork()`)
  - But fork is often immediately followed by exec
- **Idea**: Share address space between parent and child
  - Modify `fork()` to map pages copy-on-write
    - Use extra available system bits (RSW) in PTEs
  - On page fault, make a copy of the page and map it read/write
  - Need to ***refcount*** physical pages
    - Easy in xv6 but can be challenging in real OSes
      - For example, see [this article](https://lwn.net/Articles/849638/)

## Demand Paging

- **Observation**: `exec` loads the complete file into memory (see `exec.c`)
  - Expensive: takes time to do so (e.g., file is stored on a slow disk)
  - Unnecessary: maybe not the whole file will be used
- **Idea**: Load pages from the file on demand
  - Allocate page table entries, but mark them on-demand
  - On fault, read the page in from the file and update page table entry
  - Need to keep some meta information about where a page is located on disk
    - This information is typically in a structure called virtual memory area (VMA)
- **Challenge**: File larger than physical memory (see next idea)

## Use Virtual Memory Larger Than Physical Memory

- **Observation**: Application may need more memory than there is physical memory
- **Idea**: Store less-frequently used parts of the address space on disk
  - Page-in and page-out pages of the address address space transparently
- Works when working sets fit in physical memory
  - Most popular replacement strategy: **least-recently used (LRU)**
  - The Access bit in the PTE helps the kernel implementing LRU
- Replacement policies are a huge topic in OS literature
  - Many OS allow applications to influence its decisions (e.g., using `madvise` and `mlock`)
- **Demo**: Run `top` and `vmstat`
  - On laptop and `dialup.athena.mit.edu`
  - See `VIRT RES MEM SHR` columns

## Memory-Mapped Files

- **Idea**: Allow access to files using load and store
  - Can easily read and write parts of a file
  - E.g., don't have to change offset using `lseek` system call
- Unix systems have a new system call for memory-mapped files:

  ```c
  void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
  ```

- Kernel pages-in pages of a file on demand
  - When memory is full, page-out pages of a file that are not frequently used

## Page Faults for User Applications

- Many useful kernel tricks using page faults
  - Allow user apps also to do such tricks
- Linux: `mmap` and `sigaction`
- [Homework Link](https://pdos.csail.mit.edu/6.828/2018/homework/mmap.html)

## Shared Virtual Memory

- **Idea**: Allow processes on different machines to share virtual memory
  - Gives the illusion of physical shared memory, across a network
- Replicate pages that are only read
- Invalidate copies on write

## TLB Management

- CPUs cache paging translation for speed
- xv6 flushes entire TLB during user/kernel transitions
  - Why?
- RISC-V allows more sophisticated plans
  - **PTE_G**: global TLB bits
    - What page could use this?
  - **ASID numbers**
    - TLB entries are tagged with ASID, so kernel can flush selectively
    - `SATP` takes an ASID number
    - `sfence.vma` also takes an ASID number
  - **Large pages**
    - 2MB and 1GB

## Virtual Memory is Still Evolving

- **Recent Changes in Linux**
  - PKTI to handle meltdown side-channel
    - [Kernel Page-Table Isolation](https://en.wikipedia.org/wiki/Kernel_page-table_isolation)
  - xv6 basically implements KPTI
- **Somewhat Recent Changes**
  - Support for 5-level page tables (57 address bits!)
  - Support for ASIDs
- **Less Recent Changes**
  - Support for large pages
  - NX (No eXecute) PTE_X flag

## Summary

- Paging plus page tables provide a powerful layer of indirection
- You will implement COW and memory-mapped files
- xv6 is simple, but you have enough information to extrapolate
