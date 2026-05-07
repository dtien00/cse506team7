#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include "types.h"
#include <termios.h>   // struct winsize lives in <termios.h> per POSIX

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404
#define TIOCEXCL   0x540C
#define TIOCNXCL   0x540D
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONREAD   0x541B
#define TIOCINQ    FIONREAD

int ioctl(int fd, unsigned long request, ...);

#endif
