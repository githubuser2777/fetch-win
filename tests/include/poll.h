#ifndef _TEST_POLL_H
#define _TEST_POLL_H

#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef POLLIN
#define POLLIN 0x0001
#endif

static inline int poll(struct pollfd *fds, unsigned long nfds, int timeout) {
    (void)fds; (void)nfds; (void)timeout;
    return 0;
}

#endif
