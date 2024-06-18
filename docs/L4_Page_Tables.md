# Lecture 4: Page Tables

## Plan

- Address spaces
- Paging hardware
- xv6 VM code

## Virtual Memory Overview

### Today's Problem

Recall the user/kernel diagram.

- Suppose the shell has a bug: sometimes it writes to a random memory address.
- Physical memory, $0\sim 2^{64}$: apps and kernel in same memory

**Question:** How can we keep it from wrecking the kernel and other processes?

### We Want Isolated Address Spaces

- Each process has its own memory:
  - It can read and write its own memory.
  - It cannot read or write anything else.

**Challenge:** How to multiplex several address spaces onto one physical memory while maintaining isolation.

### xv6 Uses RISC-V's Paging Hardware to Implement AS's

- **Note:** Ask questions! This material is important but complex.
- Topic of Thursday's lab (and shows up in several other labs).

### Page Tables Provide a Level of Indirection for Addressing

- **Path:** CPU -> MMU -> RAM (VA -> PA)
- Software can only load/store to virtual addresses, not physical.
- The kernel tells the MMU how to map each virtual address to a physical address:
  - MMU has a *table*, indexed by VA, yielding PA (called a ***"page table"*** ).

### Different Address Space for Each Process

- We need more than one page table and need to switch:
  - MMU has a register (`satp`) that the kernel writes to change the page table.

### Page Table Location

- Page table lives in memory.
- `satp` holds the (physical) address of the current page table.
- MMU loads page table entries from memory.
- Kernel can modify the page table by writing it in memory.

### Page Table Size

- $2^{64}$ distinct virtual addresses possible.
- Not practical to have a table with $2^{64}$ entries.
- RISC-V maps **4-KB "pages"** -> page table only needs an entry per page:
  - 4 KB = **12 bits**
  - RISC-V has 64-bit addresses
  - Page table index is top $64-12=52$ bits of VA
    - Except the top 25 of the top 52 are unused (future growth).
    - Index is **27 bits**.

### Simplified View (Figure 3.1)

- MMU uses index bits of VA to find a **page table entry (PTE)**.
- MMU constructs physical address using PPN from PTE + offset of VA.

### Page Table Entry (PTE)

- Structure: `[10 reserved | 44 PPN | 10 flags]`
- Each PTE is 64 bits, but only 54 are used.
- **44-bit PPN (physical page number)** is top bits of **56-bit physical address**.
- Low 10 bits of PTE are ***flags*** (Valid, Writeable, etc).
- Low 12 bits of physical address are copied from virtual address.

### Array of PTEs (Figure 3.1)

- **Question:** Would it be reasonable for the page table to just be an array of PTEs directly indexed by 27 index bits of virtual addresses?
  - Size: $2^{27}$ (roughly 134 million entries)
  - 64 bits per entry
  - Roughly 1GB per page table
  - Wastes lots of memory for small programs (many entries would consume RAM but not be needed).

### RISC-V 64 Uses a "Three-Level Page Table" to Save Space

- **Figure 3.2:**
  - High 9 bits of VA index into level-one page directory.
  - PTE from level one has physical address of level-two page directory.
  - 2nd 9 bits index level-two directory.
  - Same for 3rd 9 bits.
  - Finally, we have PTE for the page with desired memory.

- **Tree Structure:** Descended 9 bits at a time.

### Tree-Shaped Page Table

- Saves space by only using needed entries.

### Why 9 Bits?

- 9 bits -> 512 PTEs -> 64 bits / PTE -> 4096 bytes (one page).
- 9 bits means a directory fits on a single page.

### Flags in PTE

- V, R, W, X, U

### Page Faults

- If V bit not set or store and W bit not set:
  - Triggers a "page fault"
  - Forces transfer to kernel (`trap.c` in xv6 source)
  - xv6 kernel prints error, kills process:

    ```
    "usertrap(): unexpected scause ... pid=... sepc=... stval=..."
    ```

  - Kernel could install a PTE, resume the process (e.g., after loading the page of memory from disk).

## Virtual Memory in xv6

### Kernel Page Table (Figure 3.3)

- Left side is virtual.
- Right side is physical.

### Physical Address Layout

- Usually defined by hardware (the board).
- RAM and memory-mapped device registers.
- QEMU simulates the board and thus the physical address layout:
  - <https://github.com/qemu/qemu/blob/master/hw/riscv/virt.c>
  - MROM, UART, VIRTIO, DRAM (same as the right-hand side of Figure 3.3).

### Kernel's Page Table

- Defined by the kernel's page table, set up while booting.
- Mostly "direct mapping":
  - Allows the kernel to use physical address as virtual address.
  - No W bit for kernel text.
  - No X bit for kernel data, etc.
  - xv6 assumes 128 MB of RAM (`PHYSTOP = 0x88000000`).

- High pages have *two* virtual mappings.
- Kernel executes in the trampoline when switching page tables.
- Creates user page tables with identical trampoline at the same VA.

### Running Kernel Without Paging

- Often possible (depends on CPU design).
- **Why page the kernel?**
  - Put RAM where expected.
  - Double mappings.
  - Forbid some accesses to catch bugs.

### Each Process's Address Space

- Kernel makes a separate page table per process (Figure 3.4).
- Kernel switches page tables (sets `satp`) when switching processes.
- Different processes have similar virtual address layouts but map to different physical addresses in RAM.

### User Address Space Arrangement

- User virtual addresses start at zero (predictable, easier for the compiler to generate code).
- Contiguous addresses are good for big arrays (no fragmentation problem).
- Lots of address range to grow.
- Both kernel and user map the trampoline page (eases transition user -> kernel and back).
- U bit not set.

**Kernel Using User Virtual Addresses (e.g., passed to `read()`):**

- Kernel software must translate to kernel virtual address, consulting that process's page table.

## Code Walkthrough

### Setup of Kernel Address Space

- Paging is not enabled when the kernel starts, so addresses are physical.
- Kernel is compiled/linked to start at `0x80000000`, where there's RAM.
- Kernel must first create its own page table:
  - `kvmmake()` in `vm.c` (building Figure 3.3).
  - UART0 at `pa=0x10000000`, want to direct-map at `va=0x10000000`.
  - `kvmmap()` adds PTEs to a page table under construction (we're not using it yet, it's just data in memory).

**Printing the Resulting Page Table (`vmprint()`):**

- Draw tree, note first PTE's PPN indicates the 2nd-level location, etc.
- Page directory pages came from `kalloc()` (sequential due to the way `kinit()` worked).
- Does the 0..128..0 correspond to `va=0x10000000`?

  ```
  L2=0 | L1=128 | L0=0 | offset=0
  (gdb) print/x 128 << (9+12)
  $3 = 0x10000000
  ```

- VA is `128 << (12+9) = 0x10000000`, as expected.

**UART's Last-Level PTE and Expected Physical Address:**

```
(gdb) print/x (0x10000000 >> 12) << 10
$2 = 0x4000000
```

- What is the 7 in the low bits of the PTE?

**With Two Pages Mapped:**

- Move `vmprint` to after VIRTIO0 is mapped.

### Full Kernel Page Table

- Too big to print this way.
- QEMU page table in `satp`:

  ```
  ^a c
  info mem
  Note UART, RAM, trampoline at the very top.
  ^a c (to resume)
  ```

### `kvmmap()` Calls `mappages()` in `vm.c`

- Arguments: root PD, VA, size, PA, perm
- Adds mappings from a range of VAs to corresponding PAs.
- For each page in the range:
  - Calls `walk` to find the address of PTE.
  - Needs PTE's address (not just content) to modify.
  - `walk` will create page directory pages if needed.
  - Put the desired PA into the PTE.
  - Mark PTE as valid with `PTE_V`.

### `walk()` in `vm.c`

- Mimics how paging hardware finds the PTE for an address.
- Descends three levels, through three directory pages.
- `PX(level, va)` extracts the 9 bits at Level level.
- `pagetable` is a 512-entry array of 64-bit integers (PTEs).
- `&pagetable[PX(level, va)]` is the address of the PTE we want.

**Last Level:**

- If `PTE_V`:
  - The relevant page-table page already exists.
  - `PTE2PA` extracts the PPN from the PTE as a PA (which the kernel can use as a VA to read the next level).
- If not `PTE_V`:
  - `kalloc()` a fresh page-table page.
  - Fill in PTE with PPN (using `PA2PTE`).
  - Mark it `PTE_V`.

### User Page Table for `sh`

- Built in `exec.c`'s `exec()`.
- Different from kernel (e.g., no UART, etc.).
- Text, data, guard, stack.

**Application Allocating More Memory for Its Heap:**

- `sbrk(n)` -- a system call.
- `sysproc.c`'s `sys_sbrk()`.
- `proc.c`'s `growproc()`.
- `vm.c`'s `uvmalloc()` -- for each new page:
  - `kalloc()` to get PA of RAM for user heap.
  - `mappages()`.
- Modifying the process's user page table (not active at the moment, since we're in the kernel).


