#include "process.h"
#include "swtch.h"
#include "printk.h"
#include "trap.h"

struct proc proctab[MAX_PROCS];
struct proc *ready_queue = NULL;
struct proc *blocked_queue = NULL;
struct proc *current = NULL;
uint8_t kstacks[MAX_PROCS][KSTACK_SIZE] __attribute__((aligned(16)));
static struct context boot_context;

void schedule(void) {
    struct proc *next = proc_next_ready();
    if (next != NULL) {
        context_switch(next);
    }
}

void proc_yield(void) {
    schedule();
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