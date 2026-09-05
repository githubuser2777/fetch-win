#ifndef _TEST_TERMIOS_H
#define _TEST_TERMIOS_H

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define ICANON 0000002
#define ECHO 0000010
#define VMIN 6
#define VTIME 5

static inline int tcgetattr(int fd, struct termios *termios_p) {
    (void)fd; (void)termios_p;
    return -1;
}
static inline int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    (void)fd; (void)optional_actions; (void)termios_p;
    return -1;
}

#endif
