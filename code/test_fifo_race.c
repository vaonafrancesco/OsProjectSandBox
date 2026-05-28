#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int main() {
    unlink("test.fifo");
    mkfifo("test.fifo", 0666);
    if(fork() == 0) {
        printf("Child opening RDONLY...\n");
        int fd = open("test.fifo", O_RDONLY);
        printf("Child opened RDONLY! fd=%d\n", fd);
        return 0;
    }
    sleep(1);
    printf("Parent opening WRONLY | NONBLOCK...\n");
    int fd = open("test.fifo", O_WRONLY | O_NONBLOCK);
    if(fd < 0) perror("Parent open failed");
    else printf("Parent opened WRONLY! fd=%d\n", fd);
    return 0;
}
