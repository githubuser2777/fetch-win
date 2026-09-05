#ifndef _TEST_SYS_IOCTL_H
#define _TEST_SYS_IOCTL_H

#include <winsock2.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413
#undef FIONREAD
#define FIONREAD 0x541B

#ifndef _IOWR
#define _IOWR(g, n, t) 0
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef SIGWINCH
#define SIGWINCH 28
#endif

static inline int ioctl(int fd, unsigned long request, ...) {
    (void)fd; (void)request;
    return -1;
}

static inline char *getlogin(void) {
    return "testuser";
}

static inline int getppid(void) {
    return 1000;
}

#endif
