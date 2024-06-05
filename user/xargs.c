#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define STDIN 0
#define STDERR 2

int readStrLine(char *newArgv[MAXARG], int currentArgc) {
    char buf[1024];
    int n = 0;

    // Read one character at a time until newline or buffer capacity
    while (read(STDIN, buf + n, sizeof(char))) {
        if (n == 1023) {
            fprintf(STDERR, "argument is too long\n");
            exit(1);
        }
        
        if (buf[n] == '\n') {
            break;
        }
        n++;
    }

    buf[n] = 0;  // Null-terminate the string

    if (n == 0)
        return 0;
    
    int offset = 0;
    while (offset < n) {
        newArgv[currentArgc++] = buf + offset;
        while (buf[offset] != ' ' && offset < n) {
            offset++;
        }
        while (buf[offset] == ' ' && offset < n) {
            buf[offset++] = 0;
        }
    }

    return currentArgc;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        fprintf(STDERR, "Usage: xargs <command...>\n");
        exit(1);
    }

    char *newArgv[MAXARG];

    for (int i = 1; i < argc; ++i) {
        newArgv[i - 1] = argv[i];
    }

    int currentArgc;

    while ((currentArgc = readStrLine(newArgv, argc - 1)) != 0) {
        newArgv[currentArgc] = 0;
        if (fork() == 0) {
            exec(newArgv[0], newArgv);
            fprintf(STDERR, "exec failed\n");
            exit(1);
        }
        wait((int*)0);
    }

    exit(0);
}
