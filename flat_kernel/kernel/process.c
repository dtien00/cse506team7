#include "process.h"
#include "swtch.h"
#include "printk.h"
#include "trap.h"

#define KSTACK_SIZE 4096

struct proc proctab[MAX_PROCS];
struct proc *ready_queue = NULL;
struct proc *blocked_queue = NULL;
struct proc *current = NULL;
static struct context boot_context;
static uint8_t kstacks[MAX_PROCS][KSTACK_SIZE] __attribute__((aligned(16)));

/**
 * Default entry point for new kernel threads.
 * When swtch restores ra to this function, the new process starts here.
 */
static void proc_entry(void) {
    interrupts_on();
    printk("[proc %d] started\n", current->pid);
    while (1)
        asm volatile("wfi");
}

struct proc* proc_alloc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].state == PROC_ZOMBIE || proctab[i].pid == 0) {
            return &proctab[i];
        }
    }
    return NULL;
}

void proc_ready(struct proc *p) {
    p->state = PROC_READY;
    p->next = NULL;
    if (ready_queue == NULL) {
        ready_queue = p;
    } else {
        struct proc *tail = ready_queue;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
}

struct proc* proc_next_ready(void) {
    struct proc *p = ready_queue;
    if (p) {
        ready_queue = p->next;
        p->next = NULL;
    }
    return p;
}

void proc_block(struct proc *p) {
    p->state = PROC_BLOCKED;
    if (ready_queue && ready_queue->pid == p->pid) {
        ready_queue = ready_queue->next;
        p->next = NULL;
    } else {
        struct proc *cur = ready_queue;
        struct proc *prev = NULL;
        while (cur) {
            if (cur->pid == p->pid) {
                prev->next = cur->next;
                break;
            }
            prev = cur;
            cur = cur->next;
        }
    }
    if (blocked_queue == NULL) {
        blocked_queue = p;
    } else {
        struct proc *tail = blocked_queue;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
}

void proc_unblock(struct proc *p) {
    if (blocked_queue && blocked_queue->pid == p->pid) {
        blocked_queue = blocked_queue->next;
    } else {
        struct proc *c = blocked_queue;
        while (c && c->next) {
            if (c->next->pid == p->pid) {
                c->next = c->next->next;
                break;
            }
            c = c->next;
        }
    }
    p->next = NULL;
    proc_ready(p);
}

void context_switch(struct proc *next) {
    struct proc *prev = current;

    if (prev != NULL && prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
        proc_ready(prev);
    }

    current = next;
    current->state = PROC_RUNNING;

    if (prev)
        swtch(&prev->context, &next->context);
    else
        swtch(&boot_context, &next->context);
}

void proc_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proctab[i].pid = 0;
        proctab[i].state = PROC_UNUSED;
        proctab[i].next = NULL;
    }

    struct proc *init = proc_alloc();
    int idx = init - proctab;
    init->pid = 1;
    strncpy(init->name, "init", MAX_NAME - 1);
    init->state = PROC_READY;
    init->context.sp = (uint64_t)&kstacks[idx][KSTACK_SIZE];
    init->context.ra = (uint64_t)proc_entry;

    proc_ready(init);
    current = NULL;
}

void schedule(void) {
    struct proc *next = proc_next_ready();
    if (next) {
        context_switch(next);
    }
}

struct proc* proc_current(void) {
    return current;
}

struct proc* proc_find(pid_t pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].pid == pid) return &proctab[i];
    }
    return NULL;
}

void proc_yield(void) {
    schedule();
}

struct proc* proc_create(const char *name) {
    struct proc *p = proc_alloc();
    if (!p) return NULL;

    int idx = p - proctab;
    static uint32_t next_pid = 2;
    p->pid = next_pid++;
    strncpy(p->name, name, MAX_NAME - 1);
    p->state = PROC_CREATED;
    p->context.sp = (uint64_t)&kstacks[idx][KSTACK_SIZE];
    p->context.ra = (uint64_t)proc_entry;

    proc_ready(p);
    return p;
}

void proc_free(struct proc *p) {
    if (!p) return;
    p->pid = 0;
    p->state = PROC_UNUSED;
    p->next = NULL;
}

void proc_exit(int code) {
    if (current) {
        current->exit_code = code;
        current->state = PROC_ZOMBIE;
        schedule();
    }
}

void proc_kill(struct proc *p) {
    if (p) p->state = PROC_ZOMBIE;
}

void proc_wait(struct proc *p) {
    if (p) p->state = PROC_BLOCKED;
}

void proc_dump(void) {
    printk("\nProcess table:\n");
    printk("PID  State      Name\n");
    printk("---  ---------  ----------------\n");

    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].state != PROC_UNUSED) {
            const char *state_str;
            switch (proctab[i].state) {
                case PROC_CREATED:  state_str = "CREATED"; break;
                case PROC_READY:    state_str = "READY"; break;
                case PROC_RUNNING:  state_str = "RUNNING"; break;
                case PROC_BLOCKED:  state_str = "BLOCKED"; break;
                case PROC_ZOMBIE:   state_str = "ZOMBIE"; break;
                default:            state_str = "UNKNOWN"; break;
            }
            printk("%3d  %-9s  %s\n", proctab[i].pid, state_str, proctab[i].name);
        }
    }
}
