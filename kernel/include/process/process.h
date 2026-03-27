#pragma once

#include "kernel/types.h"
#include "mm/vmm.h"
#include "mm/vma.h"
#include "trap/trap.h"

#define MAX_PROCS 64
#define MAX_NAME 16
#define KSTACK_SIZE 4096

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
    void *chan; // Sleep/wakeup channel support

    uint64_t time_slice;
    uint64_t total_time;
    struct proc *next;

    // Kernel scheduler context (callee-saved regs, used by swtch)
    struct context context;

    // Trap frame (for user mode)
    trap_frame *trapframe;

    // Process hierarchy
    struct proc *parent;
    struct proc *first_child;
    struct proc *sibling;

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

// Scheduling
struct proc* proc_current(void);
void proc_yield(void);
void schedule(void);
struct proc* proc_next_ready(void);

// State transitions
void proc_ready(struct proc *p);
void proc_block(struct proc *p);
void proc_unblock(struct proc *p);
void context_switch(struct proc *next);

// Debug
void proc_dump(void);

// Sleep/wakeup channel support
void proc_sleep(void *chan);
void proc_wakeup(void *chan);

// Utilities
int exec(const char *path, char *const argv[]);
