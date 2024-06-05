/**
 * Lab 1
*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: sleep <time(int)>...\n");
        exit(1);
    }

    int timeToSleep = atoi(argv[1]);
    sleep(timeToSleep);

    exit(0);
}
