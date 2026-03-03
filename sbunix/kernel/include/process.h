#pragma once
#include <stdint.h>

#define MAX_PROCS 256
#define MAX_NAME 16

extern struct proc *current; // Current executing process

typedef enum {
    PROC_UNUSED = 0,
    PROC_CREATED = 1,
    PROC_READY = 2,
    PROC_RUNNING = 3,
    PROC_BLOCKED = 4,
    PROC_ZOMBIE = 5,
} proc_state_t;

struct proc {
    uint32_t pid;
    char name[MAX_NAME];
    uint64_t *page_table;
    uintptr_t heap_top;
    
    uint64_t pc;
    uint64_t sp;
    uint64_t regs[32];
    uint64_t satp;
    
    proc_state_t state;
    int exit_code;
    
    uint64_t time_slice;
    uint64_t total_time;
    struct proc *next;
    
    // Parent/child relationships for process hierarchy
    struct proc *parent;
    struct proc *first_child;
    struct proc *sibling;
};

// Core process management
struct proc* proc_alloc(void);
void proc_free(struct proc *p);
void proc_init(void);
struct proc* proc_find(uint32_t pid);

// Lifecycle
struct proc* proc_create(const char *name);
void proc_exit(int code);
void proc_kill(struct proc *p);
void proc_wait(struct proc *p);

// Scheduling
struct proc* proc_current(void);
void proc_yield(void);
struct proc* proc_next_ready(void);

// State transitions
void proc_ready(struct proc *p);
void proc_block(struct proc *p);
void proc_unblock(struct proc *p);
void context_switch(struct proc *next);