#include "arch/riscv.h"
#include "kernel/printk.h"
#include "lib/string.h"
#include "mm/vmm.h"
#include "process/process.h"
#include "process/swtch.h"
#include "trap/trap.h"
#include "signal.h"

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
            if (next->pagetable) {
                // Verify the kernel VA at the current PC is reachable through
                // next's page table before switching SATP — if it isn't, the
                // CPU will hang silently after the csrw satp instruction.
                extern void schedule(void);
                uintptr_t pc = (uintptr_t)schedule;
                uintptr_t mapped_pa = vmm_translate(next->pagetable, pc);
                (void) mapped_pa;
                vmm_switch(next->pagetable);
            } else
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

// Insert p into its MLFQ level, sorted by priority (lower value = higher priority).
// Equal-priority processes are inserted at the HEAD (LIFO within a priority band),
// so a newly-forked child runs before older siblings at the same priority.
// Caller must set p->mlfq_level, p->time_slice, and p->next = NULL before calling.
static void mlfq_insert(struct proc *p) {
    int lvl = p->mlfq_level;
    if (mlfq[lvl] == NULL || p->priority <= mlfq[lvl]->priority) {
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

void proc_ready(struct proc *p) {
    if (p->state == PROC_READY) return; // Guard: already READY would corrupt the MLFQ list.
    p->state = PROC_READY;
    p->time_slice = (1 << p->mlfq_level);
    p->next = NULL;
    mlfq_insert(p);
}

/*
 * proc_preempt_if_needed — yield if a higher-priority process is waiting.
 *
 * Called by sys_nice() after a process lowers its own scheduling priority
 * (positive nice increment).  If any MLFQ level holds a process whose
 * priority number is strictly less than current's (i.e. higher scheduling
 * priority), we voluntarily yield so it can run immediately.
 *
 * This mirrors the standard POSIX expectation: a process that nice()s
 * itself down should give way to better-priority work without waiting for
 * the next timer tick.
 *
 * Interrupts are already disabled here: sys_nice() is reached through
 * handle_exception → syscall_handler, and RISC-V hardware clears
 * sstatus.SIE on every trap.
 */
void proc_preempt_if_needed(void) {
    if (!current) return;
    for (int i = 0; i < MLFQ_LEVELS; i++) {
        if (mlfq[i] && mlfq[i]->priority < current->priority) {
            // If the higher-priority process is at a deeper level than current,
            // bump current's mlfq_level to match.  Without this, current would
            // re-insert at its shallower level after proc_yield() and be picked
            // again before the higher-priority process gets the CPU.
            if (current->mlfq_level < i)
                current->mlfq_level = i;
            proc_yield();
            return;
        }
    }
}

/*
 * mlfq_boost — starvation-prevention priority reset, called every
 * MLFQ_BOOST_INTERVAL ticks by timer_handler.
 *
 * The old inline code only wrote p->mlfq_level = 0 for READY processes but
 * left them sitting in their original queue (e.g. mlfq[2]).  proc_next_ready
 * scans by queue index, so those processes kept their low scheduling priority
 * until they happened to exhaust their current time slice and were re-enqueued.
 * Under a steady stream of high-priority work they could be starved
 * indefinitely despite the "boost".
 *
 * Correct per-state handling:
 *
 *   READY   — Dequeue from current level, reset mlfq_level to 0 and
 *              time_slice to 1, reset total_time, and re-insert into
 *              mlfq[0].  The process is immediately eligible to be
 *              scheduled ahead of all lower-level work.
 *              Processes already at level 0 are skipped (no-op).
 *
 *   BLOCKED — Just reset mlfq_level to 0.  The process is not in any
 *              MLFQ queue; when it eventually wakes, proc_unblock() calls
 *              proc_ready() which will enqueue it at level 0.
 *
 *   RUNNING — Not in any queue.  Reset mlfq_level and total_time so the
 *              process re-enters the scheduler at level 0 on its next
 *              yield or preemption.
 */
void mlfq_boost(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        struct proc *p = &proctab[i];

        if (p->state == PROC_BLOCKED) {
            p->mlfq_level = 0;

        } else if (p->state == PROC_READY && p->mlfq_level > 0) {
            // Dequeue from the current (lower) level.
            int lvl = p->mlfq_level;
            if (mlfq[lvl] == p) {
                mlfq[lvl] = p->next;
            } else {
                struct proc *cur = mlfq[lvl], *prev = NULL;
                while (cur) {
                    if (cur == p) {
                        if (prev) prev->next = cur->next;
                        break;
                    }
                    prev = cur;
                    cur = cur->next;
                }
            }
            // Re-insert at level 0 with a fresh time slice.
            p->next       = NULL;
            p->mlfq_level = 0;
            p->time_slice = 1;   /* 1 << 0 */
            p->total_time = 0;
            mlfq_insert(p);

        } else if (p->state == PROC_RUNNING) {
            // Not in any queue; reset so next enqueue lands at level 0.
            p->mlfq_level = 0;
            p->total_time = 0;
        }
    }
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
    // Stop signals: SIGSTOP is unmaskable; SIGTSTP/SIGTTIN/SIGTTOU respect signal_mask.
    uint32_t pending_stop = prev->pending_signals & KERNEL_STOP_SIGS;
    uint32_t catchable_stops = pending_stop & ~(1U << KERNEL_SIGSTOP);
    pending_stop &= ~(catchable_stops & prev->signal_mask);
    if (pending_stop) {
        prev->pending_signals &= ~pending_stop;
        prev->stop_signal = (uint8_t)kernel_ctz(pending_stop);
        proc_stop(prev);
        // Resumes here after SIGCONT.
    }

    // Clear any SIG_IGN'd and default-ignore pending signals before the fatal-
    // signal check so that a sleeping process is not killed or spuriously woken.
    // SIGKILL and SIGSTOP can never be ignored (POSIX).
    {
        uint32_t tmp = prev->pending_signals & ~prev->signal_mask;
        while (tmp) {
            int s = kernel_ctz((uint32_t)(tmp));
            tmp &= tmp - 1;
            if (s == KERNEL_SIGKILL || s == KERNEL_SIGSTOP)
                continue;
            if (prev->handlers[s] == (void *)SIG_IGN ||
                ((1U << s) & KERNEL_DFL_IGNORE_SIGS &&
                 prev->handlers[s] == (void *)SIG_DFL))
                prev->pending_signals &= ~(1u << s);
        }
    }

    // Fatal signals - respect signal mask
    uint32_t unmasked = prev->pending_signals & ~prev->signal_mask;
    if (unmasked & KERNEL_FATAL_SIGS) {
        prev->pending_signals = 0;
        // Encode the specific signal so WTERMSIG returns the right number.
        // kernel_ctz is safe here: the if-guard guarantees the argument is non-zero.
        proc_exit(-kernel_ctz((uint32_t)(unmasked & KERNEL_FATAL_SIGS)));
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

    // A stop supersedes any unreported continue (POSIX: if a continued child
    // is subsequently stopped before the continue is consumed via WCONTINUED,
    // only the stop should be reported).
    p->cont_unreported = 0;

    // Indicate the signal must be stopped.
    p->stop_unreported = 1;

    // SIGCHLD to parent: notify that child stopped (POSIX WUNTRACED).
    // Must use p->parent (the stopped child's parent), not current->parent.
    if (p->parent) {
        p->parent->pending_signals |= (1u << SIGCHLD);
        // Wake parent from waitpid (sleeps on its own proc pointer)
        proc_wakeup(p->parent);
        // Also wake sigsuspend/pause channel if SIGCHLD handler is non-default
        if (p->parent->handlers[SIGCHLD] != (void *)0 /* SIG_DFL */)
            proc_wakeup(&p->parent->pending_signals);
    }

    if (p == current) {
        struct proc *prev = current;
        current = NULL;
        swtch(&prev->context, &scheduler_context);
        // Execution resumes here after proc_unstop → scheduler picks us up
    }
}


void proc_unstop(struct proc *p) {
    if (p->state != PROC_STOPPED) return;

    // Indicate uncaught continue signal
    p->cont_unreported = 1;

    proc_ready(p);  // sets PROC_READY and re-enqueues into MLFQ

    // SIGCHLD to parent: notify that child continued (POSIX WCONTINUED).
    if (p->parent) {
        p->parent->pending_signals |= (1u << SIGCHLD);
        // Wake parent from waitpid (sleeps on its own proc pointer)
        proc_wakeup(p->parent);
        // Also wake sigsuspend/pause channel if SIGCHLD handler is non-default
        if (p->parent->handlers[SIGCHLD] != (void *)0 /* SIG_DFL */)
            proc_wakeup(&p->parent->pending_signals);
    }
}
