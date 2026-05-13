#include "sys/ioctl.h"
#include "stdarg.h"
#include "termios.h"
#include "unistd.h"
#include "errno.h"

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;

    switch (request) {
        case TCGETS: {
            va_start(ap, request);
            struct termios *t = va_arg(ap, struct termios *);
            va_end(ap);
            return tcgetattr(fd, t);
        }
        case TCSETS: {
            va_start(ap, request);
            const struct termios *t = va_arg(ap, const struct termios *);
            va_end(ap);
            return tcsetattr(fd, TCSANOW, t);
        }
        case TCSETSW: {
            va_start(ap, request);
            const struct termios *t = va_arg(ap, const struct termios *);
            va_end(ap);
            return tcsetattr(fd, TCSADRAIN, t);
        }
        case TCSETSF: {
            va_start(ap, request);
            const struct termios *t = va_arg(ap, const struct termios *);
            va_end(ap);
            return tcsetattr(fd, TCSAFLUSH, t);
        }
        case TIOCGWINSZ: {
            va_start(ap, request);
            struct winsize *ws = va_arg(ap, struct winsize *);
            va_end(ap);
            if (!ws)
            {
                errno = EINVAL;
                return -1;
            }

            ws->ws_row = 24;
            ws->ws_col = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case TIOCSWINSZ:
            return 0;
        case TIOCGPGRP: {
            va_start(ap, request);
            pid_t *pgid = va_arg(ap, pid_t *);
            va_end(ap);
            if (!pgid) { errno = EINVAL; return -1; }
            pid_t p = tcgetpgrp(fd);
            if (p < 0) return -1;
            *pgid = p;
            return 0;
        }
        case TIOCSPGRP: {
            va_start(ap, request);
            const pid_t *pgid = va_arg(ap, const pid_t *);
            va_end(ap);
            if (!pgid) { errno = EINVAL; return -1; }
            return tcsetpgrp(fd, *pgid);
        }
        default:
            errno = ENOTTY;
            return -1;
    }
}

/*
 * tcgetsid(fd) — return session id of the foreground process group of the
 * terminal referenced by fd. Validates fd is a tty via tcgetattr; sets
 * errno = ENOTTY and returns -1 if it isn't. For valid tty fds, returns
 * getpid() — correct under the SBUnix model where every shell process is
 * its own session leader after setsid(). No kernel syscall required.
 */
pid_t tcgetsid(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) < 0) {
        errno = ENOTTY;
        return (pid_t)-1;
    }
    return getpid();
}
