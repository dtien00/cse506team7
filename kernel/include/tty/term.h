#pragma once
#include "kernel/types.h"

// ─────────────────────────────────────────────────────────────────────────────
// c_iflag  — input processing
// ─────────────────────────────────────────────────────────────────────────────
#define ICRNL   (1u << 0)   // map \r → \n on input
#define IXON    (1u << 1)   // XON/XOFF flow control  (^S / ^Q)

// ─────────────────────────────────────────────────────────────────────────────
// c_oflag  — output processing
// ─────────────────────────────────────────────────────────────────────────────
#define ONLCR   (1u << 0)   // map \n → \r\n on output  (needed for UART!)

// ─────────────────────────────────────────────────────────────────────────────
// c_lflag  — local / line-discipline flags
// ─────────────────────────────────────────────────────────────────────────────
#define ECHO    (1u << 0)   // echo received characters back to output
#define ECHOE   (1u << 1)   // erase: echo BS SP BS sequence
#define ICANON  (1u << 2)   // canonical (cooked) mode; raw mode when clear
#define ISIG    (1u << 3)   // generate signals on ^C / ^\ / ^Z
#define IEXTEN  (1u << 4)   // extended processing (^V literal-next)

// ─────────────────────────────────────────────────────────────────────────────
// c_cc[] control-character indices
// ─────────────────────────────────────────────────────────────────────────────
#define VEOF     0   // ^D  — EOF on empty canonical line
#define VEOL     1   // extra end-of-line  (0 = disabled)
#define VERASE   2   // DEL / ^H — erase previous character
#define VKILL    3   // ^U — erase entire line
#define VINTR    4   // ^C — SIGINT
#define VQUIT    5   // ^\ — SIGQUIT
#define VSUSP    6   // ^Z — SIGTSTP  (deferred until job-control)
#define VLNEXT   7   // ^V — literal-next
#define VMIN     8   // raw: minimum bytes before read() returns
#define VTIME    9   // raw: timeout in 0.1 s units  (0 = no timeout)
#define NCCS    10

// ─────────────────────────────────────────────────────────────────────────────
// termios structure
// ─────────────────────────────────────────────────────────────────────────────
typedef uint32_t tcflag_t;
typedef uint8_t cc_t;

struct termios {
    tcflag_t c_iflag; // input flags
    tcflag_t c_oflag; // output flags
    tcflag_t c_cflag; // hardware control (baud/parity – mostly unused)
    tcflag_t c_lflag; // local / line-discipline flags
    cc_t c_cc[NCCS];
};

// ─────────────────────────────────────────────────────────────────────────────
// tcsetattr() action values
// ─────────────────────────────────────────────────────────────────────────────
#define TCSANOW     0   // apply immediately
#define TCSADRAIN   1   // apply after output drains  (treated as NOW for now)
#define TCSAFLUSH   2   // apply after drain, discard pending input

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

// ─────────────────────────────────────────────────────────────────────────────
// Global singleton — the one UART-backed terminal
// ─────────────────────────────────────────────────────────────────────────────
extern terminal_t console_term;

void term_global_init(void);
