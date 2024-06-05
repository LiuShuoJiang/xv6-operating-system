/**
 * Lab 1
*/

#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

const int BUFFER_SIZE = 512;

#define STDERR 2

char* getFileNameFromPath(char path[]) {
    char* point = path + strlen(path);
    for (; point >= path && *point != '/'; --point)
        ;
    
    return ++point;
    // no need for blank-padded space here!
}

void findHelper(char *path, char *targetName) {
    int currentFd;
    char nextDirBuffer[BUFFER_SIZE];

    struct stat currentStat;
    struct dirent nextDir;

    currentFd = open(path, O_RDONLY);
    if (currentFd < 0) {
        fprintf(STDERR, "find: open failed: %d\n", currentFd);
        exit(1);
    }

    if (fstat(currentFd, &currentStat) < 0) {
        fprintf(STDERR, "find: cannot stat %s\n", path);
        close(currentFd);
        exit(1);
    }

    if (currentStat.type == T_FILE) {
        if (strcmp(getFileNameFromPath(path), targetName) == 0) {
            printf("%s\n", path);
        }
    } else if (currentStat.type == T_DIR) {
        strcpy(nextDirBuffer, path);
        char *pathEnd = nextDirBuffer + strlen(nextDirBuffer);
        *pathEnd = '/';
        ++pathEnd;

        while (read(currentFd, &nextDir, sizeof(struct dirent)) == sizeof(struct dirent)) {
            if (nextDir.inum == 0) {
                continue;
            }

            if (strcmp(".", nextDir.name) == 0 || strcmp("..", nextDir.name) == 0) {
                continue;
            }

            memmove(pathEnd, nextDir.name, DIRSIZ);
            pathEnd[DIRSIZ] = '\0';

            findHelper(nextDirBuffer, targetName);  // recursive find
        }
    }

    close(currentFd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(STDERR, "Correct usage: find <directory> <file name>\n");
        exit(1);
    }

    findHelper(argv[1], argv[2]);

    return 0;
}
