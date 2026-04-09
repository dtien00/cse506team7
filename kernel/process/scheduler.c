#include "arch/riscv.h"
#include "kernel/printk.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include "process/process.h"
#include "process/swtch.h"
#include "trap/trap.h"

struct proc proctab[MAX_PROCS];
// struct proc *ready_queue = NULL;
struct proc *mlfq[MLFQ_LEVELS];
struct proc *blocked_queue = NULL;
struct proc *current = NULL;
uint8_t kstacks[MAX_PROCS][KSTACK_SIZE] __attribute__((aligned(16)));

// Idle must have enough stack for a full trap frame + nested C calls.
static uint8_t idle_stack[KSTACK_SIZE] __attribute__((aligned(16)));
static struct context idle_context;
static struct context scheduler_context;

// Idle task — runs when no process is ready.
static void idle_loop(void) {
    while (1) {
        // Interrupts must be on so wfi actually wakes up.
        interrupts_on();
        // wfi halts the CPU until the next interrupt (UART, timer, etc.).
        asm volatile("wfi");
        // returns to idle loop right after interrupt is handled, so we switch to scheduler
        swtch(&idle_context, &scheduler_context);
    }
}

// Called once from proc_init() — sets up idle and scheduler contexts.
void scheduler_init(void) {
    memset(&idle_context, 0, sizeof(idle_context));
    memset(&scheduler_context, 0, sizeof(scheduler_context));
    for (int i = 0; i < MLFQ_LEVELS; i++)
        mlfq[i] = NULL;

    idle_context.ra = (uint64_t) idle_loop;
    idle_context.sp = (uint64_t) (idle_stack + sizeof(idle_stack));
    // scheduler_context.ra/sp are filled in by the first swtch call
    // that lands here from boot() — the boot stack becomes the
    // scheduler's permanent home stack.
    // that ra causes any process that switches away from the idle loop to return back to the idle loop
}

// Flow:
//   process calls schedule()
//     → swtch saves process context, restores scheduler_context
//     → scheduler finds next ready process (or idles)
//     → swtch saves scheduler_context, restores next->context
//     → next process resumes
void schedule(void) {
    while (1) {
        // Keep scheduler bookkeeping + context handoff atomic w.r.t. timer IRQs.
        interrupts_off();

        struct proc *next = proc_next_ready();
        if (next != NULL) {
            next->state = PROC_RUNNING;
            current = next;

            // Kernel execution for this process (sleep/wait resume, syscall
            // return path, then sret) must use that process's page table.
            if (next->pagetable)
                vmm_switch(next->pagetable);
            else
                vmm_switch(vmm_kernel_pagetable());

            // Do not re-enable interrupts here; new context controls that.
            swtch(&scheduler_context, &next->context);
            // Returns here when this CPU is handed back to the scheduler
            // (i.e. the process that just ran called schedule() again).
        } else {
            // No process ready — park in idle until an interrupt wakes one.
            vmm_switch(vmm_kernel_pagetable());
            // interrupts_on();
            swtch(&scheduler_context, &idle_context);
            // returns here from idle loop, and must loop back to check for processes
        }
    }
}

// proc_yield() — voluntarily give up the CPU while staying ready, and switch to scheduler context to decide what to do next
void proc_yield(void) {
    if (current == NULL) return;
    struct proc *prev = current;
    current = NULL;
    if (prev->state != PROC_ZOMBIE && prev->state != PROC_STOPPED)
        proc_ready(prev); // sets state + enqueues
    swtch(&prev->context, &scheduler_context);
}

// void context_switch(struct proc *next) {
//     struct proc *prev = current;
//
//     if (prev != NULL && prev->state == PROC_RUNNING) {
//         prev->state = PROC_READY;
//         proc_ready(prev);
//     }
//
//     current = next;
//     current->state = PROC_RUNNING;
//
//     if (prev)
//         swtch(&prev->context, &next->context);
//     else
//         swtch(&boot_context, &next->context);
// }

void proc_ready(struct proc *p) {
    p->state = PROC_READY;
    p->time_slice = (1 << p->mlfq_level);
    p->next = NULL;
    int lvl = p->mlfq_level;
    if (mlfq[lvl] == NULL || p->priority < mlfq[lvl]->priority) {
        p->next = mlfq[lvl];
        mlfq[lvl] = p;
        return;
    }
    struct proc *cur = mlfq[lvl];
    while (cur->next && cur->next->priority <= p->priority)
        cur = cur->next;
    p->next = cur->next;
    cur->next = p;
}

struct proc *proc_next_ready(void) {
    for (int i = 0; i < MLFQ_LEVELS; i++) {
        struct proc *p = mlfq[i];
        if (p) {
            mlfq[i] = p->next;
            p->next = NULL;
            return p;
        }
    }
    return NULL;
}

void proc_block(struct proc *p) {
    // If p is currently READY, remove it from its MLFQ level.
    if (p->state == PROC_READY) {
        int lvl = p->mlfq_level;
        if (mlfq[lvl] == p) {
            mlfq[lvl] = p->next;
        } else {
            struct proc *cur = mlfq[lvl];
            struct proc *prev = NULL;
            while (cur) {
                if (cur == p) {
                    prev->next = cur->next;
                    break;
                }
                prev = cur;
                cur = cur->next;
            }
        }
    }
    p->state = PROC_BLOCKED;
    p->next = NULL;
    if (blocked_queue == NULL) {
        blocked_queue = p;
    } else {
        struct proc *tail = blocked_queue;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
}

void proc_unblock(struct proc *p) {
    if (blocked_queue == p) {
        blocked_queue = blocked_queue->next;
    } else {
        struct proc *c = blocked_queue;
        while (c && c->next) {
            if (c->next == p) {
                c->next = c->next->next;
                break;
            }
            c = c->next;
        }
    }
    p->next = NULL;
    if (p->mlfq_level > 0) p->mlfq_level--;
    proc_ready(p);
}

// block current process and inactivates it; switches to scheduler context
void proc_sleep(void *chan) {
    interrupts_off();
    current->chan = chan;
    proc_block(current);
    // Hand control back to the scheduler.
    // When we're woken, swtch restores our context and we return here.
    struct proc *prev = current;
    current = NULL;
    swtch(&prev->context, &scheduler_context);
    prev->chan = NULL; // After wakeup, clear channel for the resumed proc
    interrupts_on();

    // After wakeup: if a fatal signal is pending, terminate this process.
    // proc_exit() does not return — it calls proc_yield() which switches
    // back to the scheduler.

    // SIGSTOP: process was woken (e.g., by SIGKILL delivery) but also has a
    // pending stop. Stop takes priority — re-block as STOPPED before checking fatal.
    uint32_t pending_stop = prev->pending_signals & KERNEL_STOP_SIGS;
    if (pending_stop) {
        prev->pending_signals &= ~pending_stop;
        proc_stop(prev);
        // Resumes here after SIGCONT.
    }

    // Fatal signals - respect signal mask
    uint32_t unmasked = prev->pending_signals & ~prev->signal_mask;
    if (unmasked & KERNEL_FATAL_SIGS) {
        prev->pending_signals = 0;
        proc_exit(-1);
    }
}

void proc_wakeup(void *chan) {
    for (int i = 0; i < MAX_PROCS; i++) {
        struct proc *p = &proctab[i];
        if (p->state == PROC_BLOCKED && p->chan == chan) {
            proc_unblock(p);
        }
    }
}

void proc_stop(struct proc *p) {
    // Remove from MLFQ if present
    if (p->state == PROC_READY) {
        int lvl = p->mlfq_level;
        if (mlfq[lvl] == p) {
            mlfq[lvl] = p->next;
        } else {
            struct proc *cur = mlfq[lvl], *prev = NULL;
            while (cur) {
                if (cur == p) { prev->next = cur->next; break; }
                prev = cur; cur = cur->next;
            }
        }
        p->next = NULL;
    }
    // Remove from blocked_queue if present
    else if (p->state == PROC_BLOCKED) {
        if (blocked_queue == p) {
            blocked_queue = blocked_queue->next;
        } else {
            struct proc *c = blocked_queue;
            while (c && c->next) {
                if (c->next == p) { c->next = c->next->next; break; }
                c = c->next;
            }
        }
        p->next = NULL;
        // Also clear wake_tick so timer_handler doesn't try to wake a stopped proc
        p->wake_tick = 0;
    }

    p->state = PROC_STOPPED;

    if (p == current) {
        struct proc *prev = current;
        current = NULL;
        swtch(&prev->context, &scheduler_context);
        // Execution resumes here after proc_unstop → scheduler picks us up
    }
}


void proc_unstop(struct proc *p) {
    if (p->state != PROC_STOPPED) return;
    proc_ready(p);  // sets PROC_READY and re-enqueues into MLFQ
}
