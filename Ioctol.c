#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>

#define IOCTL_MAGIC 'C'
#define PORTAL_SET_COLOUR _IOW(IOCTL_MAGIC, 1, char[256])

int main(int argc, char *argv[])
{
    int fd;
    char colour[256];
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <colour>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    strncpy(colour, argv[1], sizeof(colour) - 1);
    colour[sizeof(colour) - 1] = '\0';

    fd = open("/dev/MARKDRIVER", O_RDWR);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (ioctl(fd, PORTAL_SET_COLOUR, colour) < 0) {
        perror("ioctl");
        close(fd);
        exit(EXIT_FAILURE);
    }
    printf("Portal colour set to %s\n", colour);
    close(fd);
    return 0;

}
