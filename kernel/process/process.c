#include "process/process.h"
#include "process/swtch.h"
#include "kernel/printk.h"
#include "trap/trap.h"

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

struct proc* proc_current(void) {
    return current;
}

struct proc* proc_find(pid_t pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].pid == pid) return &proctab[i];
    }
    return NULL;
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