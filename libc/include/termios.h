#ifndef _TERMIOS_H
#define _TERMIOS_H

#include "types.h"

typedef uint32_t tcflag_t;
typedef uint8_t cc_t;

#define ICRNL   (1u << 0)
#define IXON    (1u << 1)

#define ONLCR   (1u << 0)

#define ECHO    (1u << 0)
#define ECHOE   (1u << 1)
#define ICANON  (1u << 2)
#define ISIG    (1u << 3)
#define IEXTEN  (1u << 4)

#define VEOF    0
#define VEOL    1
#define VERASE  2
#define VKILL   3
#define VINTR   4
#define VQUIT   5
#define VSUSP   6
#define VLNEXT  7
#define VMIN    8
#define VTIME   9
#define NCCS    10

struct termios {
    tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
    cc_t c_cc[NCCS];
};

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);

#endif

