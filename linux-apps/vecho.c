#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <sys/select.h>

#include <string.h>

#include "rt_vbus_user.h"

#define handle_error(msg, err) \
    do { perror(msg); exit(err); } while (0)

#define BUFLEN 1024
char buf[BUFLEN];

int main(int argc, char *argv[])
{
    int ctlfd = open("/dev/rtvbus", O_RDWR);
    if (ctlfd < 0)
        handle_error("open error", ctlfd);

    struct rt_vbus_request req;

    req.name      = "vecho";
    req.prio      = 20;
    req.is_server = 0;
    req.oflag     = O_RDWR;
    req.recv_wm.low  = 500;
    req.recv_wm.high = 1000;
    req.post_wm.low  = 500;
    req.post_wm.high = 1000;

    int rwfd = ioctl(ctlfd, VBUS_IOCREQ, &req);
    if (rwfd < 0)
        handle_error("ioctl error", rwfd);

    close(ctlfd);

    printf("send: ");
    for (int i = 1; i < argc; i++) {
        write(rwfd, argv[i], strlen(argv[i]));
        write(rwfd, " ", 1);
        printf("%s ", argv[i]);
    }
    write(rwfd, "\n", 1);
    printf("\n");
    int len = read(rwfd, buf, sizeof(buf)-1);
    buf[len] = '\0';
    printf("recv: %s\n", buf);

    close(rwfd);
    exit(0);
}


