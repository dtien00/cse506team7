#include "process/process.h"
#include "process/swtch.h"
#include "lib/string.h"
#include "trap/trap.h"
#include "kernel/printk.h"
#include "mm/vmm.h"

struct proc proctab[MAX_PROCS];
struct proc *ready_queue = NULL;
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

            swtch(&scheduler_context, &next->context);
            // Returns here when this CPU is handed back to the scheduler
            // (i.e. the process that just ran called schedule() again).
        } else {
            // No process ready — park in idle until an interrupt wakes one.
            vmm_switch(vmm_kernel_pagetable());
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
    if (prev->state != PROC_ZOMBIE)
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
    p->next = NULL;
    if (ready_queue == NULL) {
        ready_queue = p;
    } else {
        struct proc *tail = ready_queue;
        while (tail->next) tail = tail->next;
        tail->next = p;
    }
}

struct proc *proc_next_ready(void) {
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
}

void proc_wakeup(void *chan) {
    for (int i = 0; i < MAX_PROCS; i++) {
        struct proc *p = &proctab[i];
        if (p->state == PROC_BLOCKED && p->chan == chan) {
            proc_unblock(p);
        }
    }
}
