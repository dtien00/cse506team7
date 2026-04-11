#pragma once

#include "kernel/types.h"
#include "mm/vmm.h"
#include "trap/trap.h"
#include "fs/file.h"
#include "fs/vfs.h"

#define MAX_PROCS 64
#define MAX_NAME 16
#define KSTACK_SIZE 4096

// Kernel-internal signal numbers (mirrors libc/include/signal.h)
#define KERNEL_SIGINT    2
#define KERNEL_SIGKILL   9
#define KERNEL_SIGPIPE  13
#define KERNEL_SIGTERM  15
#define KERNEL_SIGCONT  18
#define KERNEL_SIGSTOP  19
// Bitmask of signals that unconditionally terminate the process
// Signals whose kernel action is proc_exit()
// SIGPIPE is here so when pipes deliver it, action is automatic.
// SIGINT is here so when the terminal driver delivers it, action is automatic.
#define KERNEL_FATAL_SIGS ((1U << KERNEL_SIGKILL) | (1U << KERNEL_SIGTERM) | \
                           (1U << KERNEL_SIGINT)  | (1U << KERNEL_SIGPIPE))

// SIGSTOP cannot be masked or ignored (POSIX)
#define KERNEL_STOP_SIGS  (1U << KERNEL_SIGSTOP)

#define MLFQ_LEVELS 4
#define MLFQ_BOOST_INTERVAL 50
#define PROC_DEFAULT_PRIORITY 5

extern struct proc *current;
extern uint8_t kstacks[][KSTACK_SIZE];

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
    uint32_t pending_signals; // bitmask: bit N set means signal N is pending
    uint32_t signal_mask;
    struct proc *next;

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
int exec(const char *path, char *const argv[]);

void fork_ret(void);