/**
Lecture 1
*/

#include "kernel/types.h"
#include "kernel/fs.h"
#include "user/user.h"

// ex9.c: list file names in the current directory

int main() {
    int fd;
    struct dirent e;

    fd = open(".", 0);
    while (read(fd, &e, sizeof(e)) == sizeof(e)) {
        if (e.name[0] != '\0') {
            printf("%s\n", e.name);
        }
    }
    exit(0);
}
