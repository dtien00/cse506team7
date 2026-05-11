#pragma once
#include "kernel/types.h"

// ─────────────────────────────────────────────────────────────────────────────
// POSIX termios bit values, c_cc indices, and struct layout.
// MUST stay byte-identical to libc/include/termios.h and
// thirdparty/include/termios.h — the struct crosses the syscall ABI verbatim
// via copy_to_user/copy_from_user in sys_tcgetattr/sys_tcsetattr.
// ─────────────────────────────────────────────────────────────────────────────

typedef uint32_t tcflag_t;
typedef uint32_t speed_t;
typedef uint8_t  cc_t;

#define NCCS 32

// c_iflag — input modes
#define IGNBRK  0x0001
#define BRKINT  0x0002
#define IGNPAR  0x0004
#define PARMRK  0x0008
#define INPCK   0x0010
#define ISTRIP  0x0020
#define INLCR   0x0040
#define IGNCR   0x0080
#define ICRNL   0x0100   // map \r → \n on input
#define IXON    0x0400   // XON/XOFF output flow control (^S / ^Q)
#define IXANY   0x0800
#define IXOFF   0x1000
#define IMAXBEL 0x2000
#define IUTF8   0x4000

// c_oflag — output modes
#define OPOST  0x0001
#define ONLCR  0x0004   // map \n → \r\n on output (needed for UART!)
#define OCRNL  0x0008
#define ONOCR  0x0010
#define ONLRET 0x0020
#define TABDLY 0x1800
#define TAB3   0x1800

// c_cflag — control modes
#define CSIZE   0x0030
#define CS5     0x0000
#define CS6     0x0010
#define CS7     0x0020
#define CS8     0x0030
#define CSTOPB  0x0040
#define CREAD   0x0080
#define PARENB  0x0100
#define PARODD  0x0200
#define HUPCL   0x0400
#define CLOCAL  0x0800
#define CRTSCTS 0x80000000U

// c_lflag — local / line-discipline modes
#define ISIG    0x0001   // generate signals on ^C / ^\ / ^Z
#define ICANON  0x0002   // canonical (cooked) mode; raw mode when clear
#define XCASE   0x0004
#define ECHO    0x0008   // echo received characters back to output
#define ECHOE   0x0010   // erase: echo BS SP BS sequence
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080
#define TOSTOP  0x0100
#define ECHOCTL 0x0200
#define ECHOPRT 0x0400
#define ECHOKE  0x0800
#define IEXTEN  0x8000   // extended processing (^V literal-next)

// c_cc[] control-character indices
#define VINTR    0   // ^C — SIGINT
#define VQUIT    1   // ^\ — SIGQUIT
#define VERASE   2   // DEL / ^H — erase previous character
#define VKILL    3   // ^U — erase entire line
#define VEOF     4   // ^D — EOF on empty canonical line
#define VTIME    5   // raw: timeout in 0.1 s units (0 = no timeout)
#define VMIN     6   // raw: minimum bytes before read() returns
#define VSTART   8   // ^Q — resume output
#define VSTOP    9   // ^S — stop output
#define VSUSP    10  // ^Z — SIGTSTP (deferred until job-control)
#define VEOL     11  // extra end-of-line (0 = disabled)
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15  // ^V — literal-next
#define VEOL2    16

// Baud rates
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

// tcsetattr() action values
#define TCSANOW     0   // apply immediately
#define TCSADRAIN   1   // apply after output drains (treated as NOW for now)
#define TCSAFLUSH   2   // apply after drain, discard pending input

struct termios {
    tcflag_t c_iflag; // input flags
    tcflag_t c_oflag; // output flags
    tcflag_t c_cflag; // hardware control (baud/parity — mostly unused)
    tcflag_t c_lflag; // local / line-discipline flags
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pending signal values  (kernel-internal, no libc signal.h needed)
// ─────────────────────────────────────────────────────────────────────────────
#define TERM_SIG_NONE   0
#define TERM_SIG_INT    2   // SIGINT  – ^C
#define TERM_SIG_QUIT   3   // SIGQUIT – ^backslash
#define TERM_SIG_SUSP  20   // SIGTSTP – ^Z

// ─────────────────────────────────────────────────────────────────────────────
// Buffer sizes
// ─────────────────────────────────────────────────────────────────────────────
#define TERM_IBUF_SIZE   256   // raw input ring  (UART ISR → line discipline)
#define TERM_LBUF_SIZE   256   // canonical line buffer

// ─────────────────────────────────────────────────────────────────────────────
// terminal_t
// ─────────────────────────────────────────────────────────────────────────────
struct proc; // forward — defined in process/process.h

typedef struct terminal {
    struct termios termios;

    // Raw input ring buffer — written by uart_handle_interrupt(), consumed by
    // term_handle_input() in cooked mode or term_read() directly in raw mode.
    char ibuf[TERM_IBUF_SIZE];
    uint32_t ihead; // next write slot  (producer = ISR)
    uint32_t itail; // next read  slot  (consumer = line discipline)

    // Canonical line buffer — characters accumulate here until \n or ^D.
    char lbuf[TERM_LBUF_SIZE];
    uint32_t llen;
    int line_ready; // 1 once a complete line (or EOF) is available

    // Signal delivered by the ISR that term_read() must act on after wakeup.
    // Written by ISR (interrupts off), read by process context
    int sig_pending; // TERM_SIG_* constant, 0 = none

    // The process currently sleeping inside term_read().  Set before
    // proc_sleep(), cleared on return.  Used by the ISR so it knows whose
    // proc_wakeup() channel to signal.
    struct proc *reader;

    // Foreground process group — shell sets this; used for signal delivery
    // once job-control is added.  0 means "send to current reader only".
    int32_t foreground_pgid;

    // pid of process that opened this terminal as controlling
    pid_t session_leader;

    // Process sleeping in sys_select() waiting for stdin to become readable.
    // Set by sys_select before sleeping on current; cleared on return.
    // The ISR calls proc_wakeup(select_waiter) to unblock the select caller.
    struct proc *select_waiter;
} terminal_t;

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

// Lifecycle
void term_init(terminal_t *t);

void term_reset(terminal_t *t); // restore cooked defaults

// Called FROM uart_handle_interrupt() — must not sleep
void term_handle_input(terminal_t *t, char c);

// Called BY sys_read()  (FILESTDIN)  — may block via proc_sleep()
int term_read(terminal_t *t, char *buf, uint32_t n);

// Called BY sys_write() (FILESTDOUT / FILESTDERR) — applies ONLCR etc.
int term_write(terminal_t *t, const char *buf, uint32_t n);

// termios syscall helpers  (SYS_TCGETATTR / SYS_TCSETATTR when you add them)
int term_tcgetattr(terminal_t *t, struct termios *out);

int term_tcsetattr(terminal_t *t, int action, const struct termios *in);

// Discard unread input
void term_flush_input(terminal_t *t);

// Return non-zero if the terminal has data ready for reading.
// Canonical mode: a complete line (line_ready). Raw mode: ibuf non-empty.
int term_has_input(terminal_t *t);

// ─────────────────────────────────────────────────────────────────────────────
// Global singleton — the one UART-backed terminal
// ─────────────────────────────────────────────────────────────────────────────
extern terminal_t console_term;

void term_global_init(void);
