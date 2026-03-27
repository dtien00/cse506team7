#include "process/process.h"
#include "process/swtch.h"
#include "kernel/printk.h"
#include "trap/trap.h"
#include "tests/test.h"

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

void proc_sleep(void *chan){
    current->chan = chan;
    proc_block(current);
    schedule(); // Switches away to yield CPU; returns here when woken
    current->chan = NULL; // After wakeup, clear channel
}

void proc_wakeup(void *chan){
    struct proc *p = blocked_queue;
    while (p) {
        struct proc *next = p->next; // save next before unblock mutates it
        if (p->chan == chan) {
            proc_unblock(p); // moves process to ready queue
        }
        p = next;
    }
}


//------------------------------//
// ******** TEST CASES ******** //
//------------------------------//

void sched_test_run(void) {
    printk("\n========================================\n");
    printk("       Scheduler Tests\n");
    printk("========================================\n\n");
    printk("=== Scheduler Core Tests ===\n");

    // Allocate two dummy processes (do not create real kernel processes)
    struct proc *p1 = proc_alloc();
    struct proc *p2 = proc_alloc();
    TEST("proc_alloc for scheduler test", p1 && p2 && p1 != p2);

    // Ready both
    if (p1) proc_ready(p1);
    if (p2) proc_ready(p2);

    // Simulate scheduling
    struct proc *n1 = proc_next_ready();
    struct proc *n2 = proc_next_ready();
    TEST("proc_next_ready returns both", n1 && n2 && n1 != n2);

    // Block and unblock
    if (n1) proc_block(n1);
    TEST("proc_block sets BLOCKED", n1 && n1->state == PROC_BLOCKED);
    proc_unblock(n1);
    TEST("proc_unblock sets READY", n1 && n1->state == PROC_READY);

    // Free dummy processes
    if (p1) proc_free(p1);
    if (p2) proc_free(p2);

    printk("\n========================================\n");
    printk("    End of Scheduler Tests\n");
    printk("========================================\n\n");
}

void sched_timeslice_test_run(void) {
    printk("[sched_timeslice_test_run] Not implemented yet.\n");
    TEST("sched_timeslice_test_run stub", 0);
}

void sched_priority_test_run(void) {
    printk("[sched_priority_test_run] Not implemented yet.\n");
    TEST("sched_priority_test_run stub", 0);
}

void sched_fairness_test_run(void) {
    printk("[sched_fairness_test_run] Not implemented yet.\n");
    TEST("sched_fairness_test_run stub", 0);
}

void sched_idle_test_run(void) {
    printk("[sched_idle_test_run] Not implemented yet.\n");
    TEST("sched_idle_test_run stub", 0);
}