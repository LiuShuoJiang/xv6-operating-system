# Lab 3: Page Tables

## Speed Up System Calls

> Refer to `proc_pagetable`, `proc_freepagetable`, `allocproc` and `freeproc` functions in [`proc.c`](../kernel/proc.c), and [`proc.h`](../kernel/proc.h).

## Print a Page Table

> Refer to [`vm.c`](../kernel/vm.c), [`exec.c`](../kernel/exec.c) and [`defs.h`](../kernel/defs.h).

## Detect Which Pages Have Been Accessed

> Refer to [`sysproc.c`](../kernel/sysproc.c) and [`riscv.h`](../kernel/riscv.h).

## Additional Notes

### About `exec`

**[`exec`](../kernel/exec.c) Function**:

1. **Initialization and Setup**:
   - The function starts by declaring several variables used for various purposes such as storing the executable path, arguments, ELF header, program header, and pagetable.
   - The current process structure is retrieved using `myproc()`.
   - The file operation begins with `begin_op()`.
2. **Pathname Resolution**:
   - The executable file is located using `namei(path)`, which converts the file path into an inode pointer.
   - If the file is not found, `end_op()` is called to end the file operation and the function returns `-1`.
3. **Read and Verify ELF Header**:
   - The inode is locked using `ilock(ip)`.
   - The ELF header is read from the file using `readi`.
   - The ELF header's magic number is checked to ensure it’s a valid ELF file. If not valid, the function jumps to the `bad` label.
4. **Create a New Page Table**:
   - A new page table is created using `proc_pagetable(p)`. If this fails, the function jumps to the `bad` label.
5. **Load Program Segments**:
   - The program headers are iterated over to load the segments into memory.
   - For each segment:
     - It reads the program header using `readi`.
     - Verifies if the segment is loadable (`ph.type == ELF_PROG_LOAD`).
     - Checks the segment sizes and alignment.
     - Allocates memory for the segment using `uvmalloc`.
     - Loads the segment into memory using `loadseg`.
6. **Release Inode and End File Operation**:
   - The inode is unlocked and released using `iunlockput(ip)`.
   - The file operation is ended with `end_op()`.
   - The `ip` is set to `0` indicating it is no longer needed.
7. **Setup User Stack**:
   - The process’s old size is saved.
   - Two pages are allocated for the stack; one is a guard page and the other is the actual stack.
   - The stack pointer (`sp`) is set up.
8. **Push Arguments onto Stack**:
   - Argument strings are copied onto the stack.
   - The array of argument pointers is pushed onto the stack.
   - The stack pointer is adjusted for alignment.
9. **Prepare for User Mode Execution**:
   - The arguments to the user’s `main` function are prepared.
   - The program name is saved for debugging purposes.
   - The new page table is committed to the process.
   - The program counter (`epc`) and stack pointer (`sp`) in the trapframe are set to the entry point of the ELF and the stack pointer respectively.
   - The old page table is freed.
10. **Print Page Table (Lab3)**:
    - If the process ID is 1, the page table is printed (specific to Lab3).
11. **Return Argument Count**:
    - The function returns the number of arguments, which becomes the first argument to the main function in the user program.

**Error Handling (`bad` label)**:

- If any step fails, the function jumps to the `bad` label.
- It frees the newly created page table if it exists.
- It releases the inode if it was locked.
- Ends the file operation if it was started.

**Load Segment (`loadseg` function)**:

The `loadseg` function loads a segment from the executable file into the appropriate location in memory.

1. **Initialization**:
   - Iterates over the segment size in page-size increments.
   - Retrieves the physical address for the given virtual address using `walkaddr`.
2. **Read Segment Data**:
   - Reads the data from the inode into the physical memory location using `readi`.
3. **Error Handling**:
   - If any part of the process fails, it returns `-1`.
   - On success, it returns `0`.

