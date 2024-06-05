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

void findPrimes(int primePipe[2]) {
    int prime;

    close(primePipe[WRITE]);

    // the first number (2, 3, 5, 7,...) is definitely prime
    if (read(primePipe[READ], &prime, sizeof(int)) != sizeof(int)) {
        fprintf(STDERR, "failed to read child process\n");
        exit(1);
    }
    printf("prime %d\n", prime);

    int num;
    int flag = read(primePipe[READ], &num, sizeof(int));

    if (flag == 0) {
        exit(0);
    } else {
        int subPipe[2];
        pipe(subPipe);

        int pid = fork();

        if (pid < 0) {
            fprintf(STDERR, "error when fork in child!\n");
            exit(1);
        } else if (pid == 0) {
            findPrimes(subPipe);
        } else {
            close(subPipe[READ]);

            if (num % prime != 0) {
                write(subPipe[WRITE], &num, sizeof(int));
            }

            while (read(primePipe[READ], &num, sizeof(int))) {
                if (num % prime != 0) {
                    write(subPipe[WRITE], &num, sizeof(int));
                }
            }

            close(primePipe[READ]);
            close(subPipe[WRITE]);
            
            wait((int*)0);
        }
    }

    exit(0);
}

int main(int argc, char *argv[]) {
    int primePipe[2];
    pipe(primePipe);

    int pid = fork();

    if (pid < 0) {
        fprintf(STDERR, "error when fork!\n");
        exit(1);
    } else if (pid == 0) {
        findPrimes(primePipe);
    } else {
        close(primePipe[READ]);
        
        for (int i = 2; i <= 35; i++) {
            if (write(primePipe[WRITE], &i, sizeof(int)) != sizeof(int)) {
                fprintf(STDERR, "The first process failed to write %d into the pipe\n", i);
				exit(1);
            }
        }
        close(primePipe[WRITE]);

        wait((int*)0);
    }
    
    exit(0);
}
