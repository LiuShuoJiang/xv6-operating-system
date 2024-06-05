/**
 * Lecture 1
 */

// ex4.c: replace a process with an executable file

#include "kernel/types.h"
#include "user/user.h"

int main() {
    // argv[0] is the name of the executable file
    // The arguments to the executable file are argv[1], argv[2], ...
    // The last element of argv must be 0, which is void pointer void*(0) == NULL
    char *argv[] = {"echo", "this", "is", "echo", 0};
    
    int exec_result = exec("echo", argv);
    if (exec_result < 0) {
        fprintf(2, "exec failed: %d\n", exec_result);
        exit(1);
    }
    
    // This line will not be reached if exec succeeds
    fprintf(2, "exec failed!\n");
    exit(1);
}
