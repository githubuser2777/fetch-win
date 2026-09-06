#ifndef _TEST_SYS_UTSNAME_H
#define _TEST_SYS_UTSNAME_H

#include <string.h>

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

static inline int uname(struct utsname *buf) {
    if (buf) {
        strcpy(buf->sysname, "Linux");
        strcpy(buf->nodename, "baseline-host");
        strcpy(buf->release, "6.8.0");
        strcpy(buf->version, "#1 SMP");
        strcpy(buf->machine, "x86_64");
    }
    return 0;
}

#endif
