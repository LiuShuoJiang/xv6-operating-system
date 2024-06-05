/**
 * Lecture 1
 */


#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// ex6.c: run a command with output redirected

int main() {
    int pid;

    // Fork a new process
    pid = fork();

    if (pid < 0) {
        // Fork failed
        fprintf(2, "fork failed: %d\n", pid);
        exit(1);
    } else if (pid == 0) {
        // Child process

        /*
        Here the standard output file descriptor 1 is turned off, 
        and then open a new file, 
        it will automatically assign the smallest unused file descriptor to this file descriptor, 
        which is 1, 
        thus realizing the redirection to the standard output.
        */
        // Close the standard output file descriptor (fd 1)
        int close_result = close(1);
        if (close_result < 0) {
            fprintf(2, "close failed: %d\n", close_result);
            exit(1);
        }

        // Open the file "out" for writing, creating it if it doesn't exist
        // and truncating it if it already exists
        /*
        The file "out" is opened for writing using open() 
        with the flags O_WRONLY (write-only), 
        O_CREATE (create if it doesn't exist), 
        and O_TRUNC (truncate if it already exists). 
        This file will be used for redirecting the output of the command.
        */
        int fd = open("out", O_WRONLY | O_CREATE | O_TRUNC);
        if (fd < 0) {
            fprintf(2, "open failed: %d\n", fd);
            exit(1);
        }

        // Prepare the arguments for the "echo" command
        char *argv[] = {"echo", "this", "is", "redirected", "echo", 0};

        // Execute the "echo" command with the specified arguments
        // echo has no idea that its output is being redirected
        int exec_result = exec("echo", argv);
        if (exec_result < 0) {
            fprintf(2, "exec failed: %d\n", exec_result);
            exit(1);
        }

        // This line will not be reached if exec succeeds
        fprintf(2, "exec failed!\n");
        exit(1);
    } else {
        // Parent process

        // Wait for the child process to exit
        int wait_result = wait((int *)0);
        if (wait_result < 0) {
            fprintf(2, "wait failed: %d\n", wait_result);
            exit(1);
        }
    }

    exit(0);
}
