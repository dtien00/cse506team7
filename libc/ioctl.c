#include "sys/ioctl.h"
#include "stdarg.h"
#include "termios.h"
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
                return -EINVAL;

            ws->ws_row = 24;
            ws->ws_col = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case TIOCSWINSZ:
            return 0;
        default:
            return -ENOTTY;
    }
}
