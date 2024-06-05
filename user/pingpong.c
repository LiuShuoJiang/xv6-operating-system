/**
 * Lab 1
*/

#include "kernel/types.h"
#include "user/user.h"

#define READ 0
#define WRITE 1

#define STDIN 0
#define STDOUT 1
#define STDERR 2

int main(int argc, char *argv[]) {
    int parentToChild[2], childToParent[2];
    char buf = 'P';
    pipe(parentToChild);
    pipe(childToParent);

    int pid = fork();
    int exitStatus = 0;

    if (pid < 0) {
        // error
        fprintf(STDERR, "fork() error!\n");

        close(childToParent[READ]);
        close(childToParent[WRITE]);
        close(parentToChild[READ]);
        close(parentToChild[WRITE]);

    } else if (pid > 0) {
        // parent
        close(parentToChild[READ]);
        close(childToParent[WRITE]);

        if (write(parentToChild[WRITE], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(STDERR, "parent write() error!\n");
            exitStatus = 1;
        }
        close(parentToChild[WRITE]);

        if (read(childToParent[READ], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(STDERR, "parent read() error!\n");
            exitStatus = 1;
        } else {
            printf("%d: received pong\n", getpid());
        }
        close(childToParent[READ]);

        exit(exitStatus);

    } else {
        // child
        close(parentToChild[WRITE]);
        close(childToParent[READ]);

        if (read(parentToChild[READ], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(STDERR, "child read() error!\n");
            exitStatus = 1;
        } else {
            printf("%d: received ping\n", getpid());
        }
        close(parentToChild[READ]);

        if (write(childToParent[WRITE], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(STDERR, "child write() error!\n");
            exitStatus = 1;
        }
        close(childToParent[WRITE]);

        exit(exitStatus);

    }

    exit(0);
}
