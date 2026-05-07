#ifndef _TERMIOS_H
#define _TERMIOS_H

#include "types.h"
#include <sys/types.h>   // pid_t (per POSIX termios.h requirement)

typedef uint32_t tcflag_t;
typedef uint32_t speed_t;
typedef uint8_t  cc_t;

#define NCCS 32

// c_iflag — input modes
#define IGNBRK  0x0001  // ignore break condition
#define BRKINT  0x0002  // signal interrupt on break
#define IGNPAR  0x0004  // ignore characters with parity errors
#define PARMRK  0x0008  // mark parity errors
#define INPCK   0x0010  // enable input parity check
#define ISTRIP  0x0020  // strip 8th bit
#define INLCR   0x0040  // map NL to CR on input
#define IGNCR   0x0080  // ignore CR
#define ICRNL   0x0100  // map CR to NL on input
#define IXON    0x0400  // enable start/stop output control
#define IXANY   0x0800  // any char restarts output
#define IXOFF   0x1000  // enable start/stop input control
#define IMAXBEL 0x2000  // (ext) ring bell on input queue full
#define IUTF8   0x4000  // (ext) input is UTF-8

// c_oflag — output modes
#define OPOST  0x0001  // post-process output
#define ONLCR  0x0004  // map NL to CR-NL on output
#define OCRNL  0x0008  // (ext) map CR to NL on output
#define ONOCR  0x0010  // (ext) suppress CR at column 0
#define ONLRET 0x0020  // (ext) NL performs CR function
#define TABDLY 0x1800  // (ext) tab delay mask
#define TAB3   0x1800  // (ext) expand tabs to spaces

// c_cflag — control modes
#define CSIZE   0x0030  // character size mask
#define CS5     0x0000
#define CS6     0x0010
#define CS7     0x0020
#define CS8     0x0030
#define CSTOPB  0x0040  // two stop bits else one
#define CREAD   0x0080  // enable receiver
#define PARENB  0x0100  // parity enable
#define PARODD  0x0200  // odd parity, else even
#define HUPCL   0x0400  // hang up on last close
#define CLOCAL  0x0800  // ignore modem status lines
#define CRTSCTS 0x80000000U  // (ext) RTS/CTS flow control

// c_lflag — local / line-discipline modes
#define ISIG    0x0001  // enable signals (INTR/QUIT/SUSP)
#define ICANON  0x0002  // canonical input (erase + kill processing)
#define XCASE   0x0004  // (ext) canonical upper/lower mapping
#define ECHO    0x0008  // enable echo
#define ECHOE   0x0010  // echo erase as BS-SP-BS
#define ECHOK   0x0020  // echo NL after KILL
#define ECHONL  0x0040  // echo NL even if ECHO off
#define NOFLSH  0x0080  // disable flush after INTR/QUIT
#define TOSTOP  0x0100  // stop bg jobs that try to write
#define ECHOCTL 0x0200  // (ext) echo ctrl chars as ^X
#define ECHOPRT 0x0400  // (ext) hardcopy-style erase
#define ECHOKE  0x0800  // (ext) erase line on KILL
#define IEXTEN  0x8000  // enable extended input character processing

// c_cc indices
#define VINTR    0   // ^C  — SIGINT
#define VQUIT    1   // ^\  — SIGQUIT
#define VERASE   2   // erase previous character
#define VKILL    3   // erase entire line
#define VEOF     4   // ^D  — EOF on empty canonical line
#define VTIME    5   // raw: read timeout (deciseconds)
#define VMIN     6   // raw: min bytes for read()
#define VSTART   8   // ^Q  — resume output
#define VSTOP    9   // ^S  — stop output
#define VSUSP    10  // ^Z  — SIGTSTP (deferred until job-control)
#define VEOL     11  // extra end-of-line (0 = disabled)
#define VREPRINT 12  // (ext) reprint pending input
#define VDISCARD 13  // (ext) toggle discard pending output
#define VWERASE  14  // (ext) word erase
#define VLNEXT   15  // ^V — literal-next
#define VEOL2    16  // alternate end-of-line

// Baud rates (encoded values; cfsetospeed/cfsetispeed use these)
#define B0       0
#define B50      1
#define B75      2
#define B110     3
#define B134     4
#define B150     5
#define B200     6
#define B300     7
#define B600     8
#define B1200    9
#define B1800   10
#define B2400   11
#define B4800   12
#define B9600   13
#define B19200  14
#define B38400  15

// tcsetattr() actions
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

// tcflush() arguments — which queue(s) to flush
#define TCIFLUSH  0   // pending input
#define TCOFLUSH  1   // untransmitted output
#define TCIOFLUSH 2   // both input and output

// tcflow() arguments — flow-control actions
#define TCOOFF    0   // suspend output
#define TCOON     1   // restart output
#define TCIOFF    2   // transmit STOP, suspend input
#define TCION     3   // transmit START, restart input

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

// Window size — moved here from <sys/ioctl.h> per POSIX (used with
// tcgetwinsize/tcsetwinsize and the legacy TIOCGWINSZ/TIOCSWINSZ ioctls).
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

speed_t cfgetispeed(const struct termios *);
speed_t cfgetospeed(const struct termios *);
int     cfsetispeed(struct termios *, speed_t);
int     cfsetospeed(struct termios *, speed_t);
int     tcdrain(int);
int     tcflow(int, int);
int     tcflush(int, int);
int     tcgetattr(int, struct termios *);
pid_t   tcgetsid(int);
int     tcgetwinsize(int, struct winsize *);
int     tcsendbreak(int, int);
int     tcsetattr(int, int, const struct termios *);
int     tcsetwinsize(int, const struct winsize *);

#endif
