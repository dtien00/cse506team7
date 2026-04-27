#pragma once

#include "kernel/types.h"
#include "mm/vmm.h"
#include "trap/trap.h"
#include "fs/file.h"
#include "fs/vfs.h"

#define MAX_PROCS 64
#define MAX_NAME 16
#define KSTACK_SIZE 4096
#define MAX_HANDLERS 32

// Kernel-internal signal numbers (mirrors libc/include/signal.h).
// These are POSIX signal numbers (1-31), NOT RISC-V exception cause codes.
// SCAUSE values (frame->scause) and signal numbers (pending_signals bitmask) are
// completely separate integer spaces and are never compared against each other.
// Numeric overlaps (e.g., SCAUSE_ILLEGAL_INST=2 and KERNEL_SIGINT=2,
// SCAUSE_ECALL_FROM_S=9 and KERNEL_SIGKILL=9) are expected and universal —
// Linux, xv6, and BSD all have the same overlap.
#define KERNEL_SIGHUP    1
#define KERNEL_SIGINT    2
#define KERNEL_SIGQUIT   3
#define KERNEL_SIGILL    4
#define KERNEL_SIGTRAP   5
#define KERNEL_SIGABRT   6
#define KERNEL_SIGBUS    7  // bus error: misaligned access, MAP_SHARED access past EOF
#define KERNEL_SIGFPE    8  // floating-point exception (software-delivered only on RISC-V)
#define KERNEL_SIGKILL   9
#define KERNEL_SIGUSR1  10
#define KERNEL_SIGSEGV  11
#define KERNEL_SIGUSR2  12
#define KERNEL_SIGPIPE  13
#define KERNEL_SIGALRM  14
#define KERNEL_SIGTERM  15
#define KERNEL_SIGCHLD  17
#define KERNEL_SIGCONT  18
#define KERNEL_SIGSTOP  19
#define KERNEL_SIGTSTP  20
#define KERNEL_SIGTTIN  21
#define KERNEL_SIGTTOU  22
#define KERNEL_SIGSYS   31

// Bitmask of signals whose POSIX SIG_DFL action is "terminate the process."
// Original set: SIGKILL, SIGTERM, SIGINT, SIGPIPE, SIGALRM, SIGQUIT, SIGILL, SIGSEGV.
// Extended (SIGS_2): SIGHUP, SIGTRAP, SIGABRT, SIGUSR1, SIGUSR2, SIGSYS.
// SIGABRT is also unmaskable (sys_sigprocmask strips it): abort() must always terminate.
// SIGTRAP is also unmaskable: masking it causes an infinite EBREAK re-fault loop.
// SIGILL/SIGSEGV remain here until user-mode sigaction frame dispatch is implemented.
#define KERNEL_FATAL_SIGS ((1U << KERNEL_SIGKILL)  | (1U << KERNEL_SIGTERM)  | \
                           (1U << KERNEL_SIGINT)   | (1U << KERNEL_SIGPIPE)  | \
                           (1U << KERNEL_SIGALRM)  | (1U << KERNEL_SIGQUIT)  | \
                           (1U << KERNEL_SIGILL)   | (1U << KERNEL_SIGSEGV)  | \
                           (1U << KERNEL_SIGHUP)   | (1U << KERNEL_SIGTRAP)  | \
                           (1U << KERNEL_SIGABRT)  | (1U << KERNEL_SIGUSR1)  | \
                           (1U << KERNEL_SIGUSR2)  | (1U << KERNEL_SIGSYS)   | \
                           (1U << KERNEL_SIGBUS)   | (1U << KERNEL_SIGFPE))

// SIGSTOP, SIGSTP, SIGTTIN, SIGTTOU cannot be masked or ignored (POSIX)
#define KERNEL_STOP_SIGS ((1U << KERNEL_SIGSTOP) | (1U << KERNEL_SIGTSTP) | \
                          (1U << KERNEL_SIGTTIN) | (1U << KERNEL_SIGTTOU))

// Signals whose POSIX default action is "ignore" (not "terminate").
// When handler == SIG_DFL, these are silently discarded — identical to SIG_IGN.
// SIGCHLD (17): child stopped/continued/exited.  POSIX says default = ignore.
#define KERNEL_DFL_IGNORE_SIGS (1U << KERNEL_SIGCHLD)

/* kernel_ctz — count trailing zeros in a non-zero 32-bit signal bitmask.
 * Returns the bit position of the lowest set bit (= the signal number,
 * since signal N occupies bit N via 1U << N).
 * Avoids __builtin_ctz, which requires libgcc (__ctzdi2/__ctzsi2) that is
 * not present in freestanding kernel builds. */
static inline int kernel_ctz(uint32_t x) {
    int n = 0;
    while (!(x & 1u)) { x >>= 1; n++; }
    return n;
}

#define MLFQ_LEVELS 4
#define MLFQ_BOOST_INTERVAL 50
#define PROC_DEFAULT_PRIORITY 5

extern struct proc *current;

struct proc_info {
    int   pid;
    int   ppid;
    char  name[16];
    int   state;    // 0=unused,1=created,2=ready,3=running,4=blocked,5=zombie,6=stopped
    int   priority; // 0 if not implemented
    uint64_t cpu_ticks;
};

// Saved context for kernel-to-kernel context switch (callee-saved regs only).
// Field order must match register save/load order in swtch.S.
struct context {
    uint64_t ra;
    uint64_t sp;
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
};

typedef enum {
    PROC_UNUSED = 0,
    PROC_CREATED = 1,
    PROC_READY = 2,
    PROC_RUNNING = 3,
    PROC_BLOCKED = 4,
    PROC_ZOMBIE = 5,
    PROC_STOPPED = 6,
} proc_state_t;

struct proc {
    pid_t pid;
    char name[MAX_NAME];

    // Memory management
    pagetable_t pagetable;
    uint64_t sz;
    struct vm_area *vma_list;  // List of memory regions

    // User-mode state (saved/restored by trap_entry/sret in trap.S)
    uint64_t pc;
    uint64_t sp;
    uint64_t regs[32];
    uint64_t satp;

    proc_state_t state;
    int exit_code;
    int priority; // UNIX Convetion: lower integer = higher priority (0 = highest)
    int mlfq_level;
    void *chan; // Sleep/wakeup channel support

    uint64_t time_slice;
    uint64_t total_time;     // ticks used in current MLFQ slice (reset on preemption)
    uint64_t utime_ticks;    // total user-mode ticks consumed (never reset)
    uint64_t children_utime; // sum of utime_ticks of all reaped children
    uint64_t wake_tick;      // 0 = not sleeping; >0 = timer_ticks value at which to wake
    uint64_t alarm_tick;    // Compared with for SIGALRM; alarm()
    uint64_t itimer_value;    // ITIMER_REAL: tick at which to fire (0 = disarmed)
    uint64_t itimer_interval; // ITIMER_REAL: reload interval in ticks (0 = one-shot)
    uint32_t pending_signals; // bitmask: bit N set means signal N is pending
    uint32_t signal_mask;
    struct proc *next;

    // Per-process kernel stack with guard page below it
    uint8_t *kstack;   // stack page — grows down from kstack + KSTACK_SIZE
    uint8_t *kguard;   // guard page — unmapped; fault here = stack overflow

    // Kernel scheduler context (callee-saved regs, used by swtch)
    struct context context;

    // Trap frame (for user mode)
    trap_frame *trapframe;
    uint8_t trapframe_owned;

    // Process hierarchy
    struct proc *parent;
    struct proc *first_child;
    struct proc *sibling;

    // File Descriptor table
    fd_table_t fds;

    fs_struct_t *fs;

    // Working directory (absolute path, always starts with '/')
    // char cwd[MAX_PATH];

    // Signal handlers
    void *handlers[32]; // existing SIG_DFL / SIG_IGN per signal
    uint32_t sig_flags[32]; // sa_flags bits stored per signal

    // Process group and session management
    pid_t pgid;
    pid_t sid;
    uint8_t is_session_leader;

    // Background/Foreground process
    uint8_t stop_unreported;
    uint8_t cont_unreported;
    uint8_t stop_signal;        // which signal caused the stop (for WSTOPSIG)
    uint8_t auto_reap;          // SA_NOCLDWAIT: free without zombie on exit

    uintptr_t copy_fault_recover; // 0 when not inside a copy function; set by copy_{to,from}_user
};

extern struct proc proctab[];

// Core process management
struct proc* proc_alloc(void);
void proc_free(struct proc *p);
void proc_init(void);
struct proc* proc_find(pid_t pid);

// Lifecycle
struct proc* proc_create(const char *name);
void proc_exit(int code);
void proc_kill(struct proc *p);
int proc_wait(int *exit_code_out);
int proc_waitpid(pid_t target_pid, int *exit_code_out, int options);

// Scheduling
void scheduler_init(void);
struct proc* proc_current(void);
void proc_yield(void);
void schedule(void);
struct proc* proc_next_ready(void);

// State transitions
void proc_ready(struct proc *p);
void proc_preempt_if_needed(void);
void proc_block(struct proc *p);
void proc_unblock(struct proc *p);
void mlfq_boost(void);

// Debug
void proc_dump(void);

// Sleep/wakeup channel support
void proc_sleep(void *chan);
void proc_wakeup(void *chan);
void proc_stop(struct proc *p);
void proc_unstop(struct proc *p);

// Utilities
int exec(const char *path, char *const argv[], char *const envp[]);

void fork_ret(void);