/**
 * Lecture 1
*/

// ex2.c: create a file, write to it.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main() {
    int fd = open("out", O_WRONLY | O_CREATE | O_TRUNC);

    if (fd < 0) {
        fprintf(2, "open failed: %d\n", fd);
        exit(1);
    }

    printf("open returned fd %d\n", fd);

    int bytes_written = write(fd, "ooo\n", 4);
    if (bytes_written != 4) {
        fprintf(2, "write failed: %d\n", bytes_written);
        close(fd);
        exit(1);
    }
    
    int close_result = close(fd);
    if (close_result < 0) {
        fprintf(2, "close failed: %d\n", close_result);
        exit(1);
    }
    
    exit(0);
}
