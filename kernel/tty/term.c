#include "tty/term.h"
#include "arch/riscv.h"
#include "kernel/console.h"
#include "kernel/printk.h"
#include "process/process.h"
#include "lib/string.h"

// ─────────────────────────────────────────────────────────────────────────────
// Global singleton
// ─────────────────────────────────────────────────────────────────────────────
terminal_t console_term;

// ─────────────────────────────────────────────────────────────────────────────
// Default cooked-mode settings
// ─────────────────────────────────────────────────────────────────────────────
static const struct termios TERMIOS_DEFAULT = {
    .c_iflag = ICRNL,
    .c_oflag = ONLCR,
    .c_cflag = 0,
    .c_lflag = ECHO | ECHOE | ICANON | ISIG,
    .c_cc = {
        [VEOF]   = 4,    // ^D
        [VEOL]   = 0,    // disabled
        [VERASE] = 127,  // DEL  (we also handle \b = 8 explicitly)
        [VKILL]  = 21,   // ^U
        [VINTR]  = 3,    // ^C
        [VQUIT]  = 28,   // ^backslash
        [VSUSP]  = 26,   // ^Z  (deferred)
        [VLNEXT] = 22,   // ^V  (deferred)
        [VMIN]   = 1,
        [VTIME]  = 0,
    },
};

// ─────────────────────────────────────────────────────────────────────────────
// Raw input ring-buffer helpers
// These are called from both ISR context and process context.
// ─────────────────────────────────────────────────────────────────────────────
static inline int ibuf_empty(terminal_t *t)
{
    return t->ihead == t->itail;
}

static inline int ibuf_full(terminal_t *t)
{
    return ((t->ihead + 1) % TERM_IBUF_SIZE) == t->itail;
}

static inline void ibuf_push(terminal_t *t, char c)
{
    if (!ibuf_full(t)) {
        t->ibuf[t->ihead] = c;
        t->ihead = (t->ihead + 1) % TERM_IBUF_SIZE;
    }
    // Drop silently when full — writer (ISR) must never block
}

static inline int ibuf_pop(terminal_t *t, char *out)
{
    if (ibuf_empty(t))
        return -1;
    *out = t->ibuf[t->itail];
    t->itail = (t->itail + 1) % TERM_IBUF_SIZE;
    return 0;
}

static inline uint32_t ibuf_count(terminal_t *t)
{
    return (t->ihead - t->itail + TERM_IBUF_SIZE) % TERM_IBUF_SIZE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Output helper — applies c_oflag processing then calls console_write().
// Safe to call from both ISR (for echo) and process context (for term_write).
// ─────────────────────────────────────────────────────────────────────────────
static void outchar(terminal_t *t, char c)
{
    if (c == '\n' && (t->termios.c_oflag & ONLCR)) {
        char cr = '\r';
        console_write(1, &cr);
    }
    console_write(1, &c);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void term_init(terminal_t *t)
{
    t->termios        = TERMIOS_DEFAULT;
    t->ihead          = 0;
    t->itail          = 0;
    t->llen           = 0;
    t->line_ready     = 0;
    t->sig_pending    = TERM_SIG_NONE;
    t->reader         = NULL;
    t->foreground_pgid = 0;
}

void term_reset(terminal_t *t)
{
    t->termios = TERMIOS_DEFAULT;
    term_flush_input(t);
}

// shim so term_global_init can pass a plain function pointer
static void term_receive(char c) {
    term_handle_input(&console_term, c);
}

void term_global_init(void) {
    term_init(&console_term);
    console_set_input_handler(term_receive);  // register with console layer
    interrupts_enable(SIE_EXTERNAL);
    printk("tty: console terminal initialized\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// term_write — process context (sys_write for stdout/stderr)
// Applies output processing (ONLCR) and writes through the console layer.
// ─────────────────────────────────────────────────────────────────────────────
int term_write(terminal_t *t, const char *buf, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        outchar(t, buf[i]);
    return (int)n;
}

// ─────────────────────────────────────────────────────────────────────────────
// term_handle_input — called FROM uart_handle_interrupt(), ISR context.
//
// This is the heart of the line discipline.  It must NEVER call proc_sleep().
// It may call proc_wakeup() which is safe from ISR context (it only moves
// processes between queues and never touches the current register context).
//
// Flow:
//   1. Input translations     (c_iflag)
//   2. Signal generation      (c_lflag ISIG)
//   3a. Canonical line editing (c_lflag ICANON)
//   3b. Raw mode buffering    (!ICANON)
// ─────────────────────────────────────────────────────────────────────────────
void term_handle_input(terminal_t *t, char c)
{
    struct termios *tm = &t->termios;

    // ── 1. Input translations ────────────────────────────────────────────────
    if (c == '\r' && (tm->c_iflag & ICRNL))
        c = '\n';

    // ── 2. Signal generation ─────────────────────────────────────────────────
    // These fire before any buffering so a stuck read() is always interruptible.
    if (tm->c_lflag & ISIG) {
        int sig = TERM_SIG_NONE;

        if (c == (char)tm->c_cc[VINTR])   // ^C → SIGINT
            sig = TERM_SIG_INT;
        else if (c == (char)tm->c_cc[VQUIT])  // ^\ → SIGQUIT
            sig = TERM_SIG_QUIT;

        if (sig != TERM_SIG_NONE) {
            // Print visual indicator (^C or ^\)
            if (tm->c_lflag & ECHO) {
                const char *label = (sig == TERM_SIG_INT) ? "^C\n" : "^\\\n";
                console_write(3, label);
            }
            // Record signal for term_read() to act on in process context
            t->sig_pending = sig;
            // Discard any partial line so the next prompt starts clean
            t->llen       = 0;
            t->line_ready = 0;
            // Wake the sleeping reader — it will see sig_pending and exit
            proc_wakeup(t);
            return;
        }
    }

    // ── 3a. Canonical (cooked) mode ──────────────────────────────────────────
    if (tm->c_lflag & ICANON) {

        // ERASE — delete previous character  (DEL or ^H)
        if (c == (char)tm->c_cc[VERASE] || c == '\b') {
            if (t->llen > 0) {
                t->llen--;
                if (tm->c_lflag & ECHOE)
                    console_write(3, "\b \b");
            }
            return;
        }

        // KILL — erase entire line  (^U)
        if (c == (char)tm->c_cc[VKILL]) {
            while (t->llen > 0) {
                t->llen--;
                if (tm->c_lflag & ECHOE)
                    console_write(3, "\b \b");
            }
            return;
        }

        // EOF on an empty line  (^D) — wake reader to return 0 bytes
        if (c == (char)tm->c_cc[VEOF] && t->llen == 0) {
            t->line_ready = 1;
            proc_wakeup(t);
            return;
        }

        // Buffer the character (drop if line buffer full)
        if (t->llen < TERM_LBUF_SIZE - 1) {
            t->lbuf[t->llen++] = c;
            // only local echo if characters are successfully buffered into input
            if (tm->c_lflag & ECHO)
                outchar(t, c);
        }

        // Newline or extra EOL character — line is complete
        if (c == '\n' || (tm->c_cc[VEOL] && c == (char)tm->c_cc[VEOL])) {
            t->line_ready = 1;
            proc_wakeup(t);
        }
        return;
    }

    // ── 3b. Raw mode ─────────────────────────────────────────────────────────
    ibuf_push(t, c);

    if (tm->c_lflag & ECHO)
        outchar(t, c);

    // Wake reader when VMIN bytes are available.
    // VMIN=0 means non-blocking; the reader polls and term_handle_input
    // still wakes in case the reader is sleeping on a VTIME timeout (future).
    uint8_t vmin = tm->c_cc[VMIN];
    if (vmin == 0 || ibuf_count(t) >= (uint32_t)vmin)
        proc_wakeup(t);
}

// ─────────────────────────────────────────────────────────────────────────────
// term_read — process context (sys_read for FILESTDIN)
//
// Canonical mode: block until a full line is in lbuf[], copy it out.
// Raw mode:       block until VMIN bytes in ibuf[], copy up to n bytes out.
//
// Returns number of bytes copied, or -1 if the process was interrupted.
// A return of 0 means EOF (^D on an empty line).
// ─────────────────────────────────────────────────────────────────────────────
int term_read(terminal_t *t, char *buf, uint32_t n)
{
    struct termios *tm = &t->termios;

    if (tm->c_lflag & ICANON) {
        // ── Cooked mode ──────────────────────────────────────────────────────
        t->reader = current;

        // Sleep until the ISR sets line_ready or sig_pending
        while (!t->line_ready && t->sig_pending == TERM_SIG_NONE)
            proc_sleep(t);     // sleeps on (t) as channel

        t->reader = NULL;

        // A signal arrived while we were sleeping (e.g. ^C)
        if (t->sig_pending != TERM_SIG_NONE) {
            // Clear so the next read doesn't re-trigger
            t->sig_pending = TERM_SIG_NONE;
            // Terminate the current process.
            // proc_exit() calls schedule() and does not return.
            proc_exit(-1);
            return -1;   // unreachable; keeps the compiler happy
        }

        // EOF: ^D on an empty line — return 0 to signal EOF to the caller
        if (t->llen == 0) {
            t->line_ready = 0;
            return 0;
        }

        // Copy canonical line to the caller's buffer.
        // SUM must be enabled at copy time (not across proc_sleep).
        uint32_t tocopy = (t->llen < n) ? t->llen : n;
        set_csr_bit(sstatus, SSTATUS_SUM);
        memcpy(buf, t->lbuf, tocopy);
        clear_csr_bit(sstatus, SSTATUS_SUM);

        // Reset line buffer so the next read starts fresh
        t->llen       = 0;
        t->line_ready = 0;
        return (int)tocopy;

    } else {
        // ── Raw mode ─────────────────────────────────────────────────────────
        uint8_t vmin = tm->c_cc[VMIN];

        if (vmin == 0) {
            // Non-blocking: return whatever is already in the buffer
            uint32_t got = 0;
            char ch;
            set_csr_bit(sstatus, SSTATUS_SUM);
            while (got < n && ibuf_pop(t, &ch) == 0)
                buf[got++] = ch;
            clear_csr_bit(sstatus, SSTATUS_SUM);
            return (int)got;
        }

        // Blocking: sleep until VMIN bytes are available
        t->reader = current;
        while (ibuf_count(t) < (uint32_t)vmin && t->sig_pending == TERM_SIG_NONE)
            proc_sleep(t);
        t->reader = NULL;

        if (t->sig_pending != TERM_SIG_NONE) {
            t->sig_pending = TERM_SIG_NONE;
            proc_exit(-1);
            return -1;
        }

        // Drain up to n bytes from the ring buffer
        uint32_t got = 0;
        char ch;
        set_csr_bit(sstatus, SSTATUS_SUM);
        while (got < n && ibuf_pop(t, &ch) == 0)
            buf[got++] = ch;
        clear_csr_bit(sstatus, SSTATUS_SUM);
        return (int)got;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// termios get/set
// ─────────────────────────────────────────────────────────────────────────────
int term_tcgetattr(terminal_t *t, struct termios *out)
{
    if (!out)
        return -1;
    *out = t->termios;
    return 0;
}

int term_tcsetattr(terminal_t *t, int action, const struct termios *in)
{
    if (!in)
        return -1;
    if (action == TCSAFLUSH)
        term_flush_input(t);
    t->termios = *in;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Flush
// ─────────────────────────────────────────────────────────────────────────────
void term_flush_input(terminal_t *t)
{
    t->ihead      = 0;
    t->itail      = 0;
    t->llen       = 0;
    t->line_ready = 0;
    // Leave sig_pending alone — a pending signal should survive a flush
}
