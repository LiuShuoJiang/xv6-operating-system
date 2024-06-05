/**
 * Lecture 1
*/

#include "kernel/types.h"
#include "user/user.h"

// ex5.c: fork then exec

int main() {
    int pid, status;

    // Fork a new process
    pid = fork();

    if (pid < 0) {
        // Fork failed
        fprintf(2, "fork failed: %d\n", pid);
        exit(1);
    } else if (pid == 0) {
        // Child process
        char *argv[] = {"echo", "THIS", "IS", "ECHO!!!", 0};
        
        // Execute the "echo" program with the specified arguments
        int exec_result = exec("echo", argv);
        
        if (exec_result < 0) {
            // Exec failed
            fprintf(2, "exec failed: %d\n", exec_result);
            exit(1);
        }
        
        // This line will not be reached if exec succeeds
        fprintf(2, "exec failed!\n");
        exit(1);
    } else {
        // Parent process
        printf("parent waiting\n");
        
        // Wait for the child process to exit and retrieve its status
        int wait_result = wait(&status);
        
        if (wait_result < 0) {
            // Wait failed
            fprintf(2, "wait failed: %d\n", wait_result);
            exit(1);
        }
        
        // // Check the exit status of the child process
        // if (WIFEXITED(status)) {
        //     // Child process exited normally
        //     int exit_status = WEXITSTATUS(status);
        //     printf("the child exited with status %d\n", exit_status);
        // } else {
        //     // Child process exited abnormally
        //     printf("the child exited abnormally\n");
        // }

        printf("the child exited with status %d\n", status);
    }

    exit(0);
}
