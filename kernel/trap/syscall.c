#include "trap/syscall.h"
#include "drivers/rtc.h"
#include "arch/riscv.h"
#include "lib/uaccess.h"
#include "process/process.h"
#include "errno.h"
#include "kernel/printk.h"
#include "kernel/resource.h"
#include "lib/string.h"
#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/vma.h"
#include "trap/trap.h"
#include "fs/block_cache.h"
#include "signal.h"
#include "sigframe.h"
#include "tty/term.h"
#include "kernel/limits.h"
/* _SC_* not included here: use SBUNIX_SC_* from kernel/limits.h in sys_sysconf */

/* Defined in trap.c; dispatches a user-space signal handler via sigframe. */
void signal_dispatch(trap_frame *frame, int sig);

/* Forward declarations for functions defined later in this file */
static long pathconf_query(int name);
long sys_pathconf(const char *upath, int name);
long sys_fpathconf(int fd, int name);


#define MAP_FAILED ((void *) -1)
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10

#define F_DUPFD          0
#define F_GETFD          1
#define F_SETFD          2
#define F_GETFL          3
#define F_SETFL          4
#define F_DUPFD_CLOEXEC  1030

#define USER_HEAP_START 0x11000000UL
#define USER_HEAP_END   MAXVA  /* Sv39 user VA ceiling; device VMAs block kernel/MMIO ranges */
/* mmap allocations start well above the heap so that sbrk growth and
 * anonymous/file mmap regions never share the same VA range.
 * The PCI MMIO reservation ends at 0x50000000; leave the first post-device
 * range available for explicit MAP_FIXED callers instead of consuming it with
 * automatic mmap placement. */
#define USER_MMAP_START 0x51000000UL

#define MAX_ARGS 64
#define KARG_MAX 256

void syscall_init(void) { printk("Syscall: Initialized\n"); }

void syscall_handler(trap_frame *frame) {
    // Make live trapframe accessible to exec() and sys_fork().
    // If an old heap snapshot exists (fork child bootstrap), drop it now.
    if (current) {
        if (current->trapframe && current->trapframe_owned)
            kfree(current->trapframe);
        current->trapframe = frame;
        current->trapframe_owned = 0;
    }

    if (current) {
        // Stop signals: SIGSTOP is unmaskable; SIGTSTP/SIGTTIN/SIGTTOU respect signal_mask.
        uint32_t pending_stop = current->pending_signals & KERNEL_STOP_SIGS;
        // Keep masked catchable stop signals pending (only SIGSTOP is unmaskable)
        uint32_t catchable_stops = pending_stop & ~(1U << KERNEL_SIGSTOP);
        pending_stop &= ~(catchable_stops & current->signal_mask);
        if (pending_stop) {
            current->pending_signals &= ~pending_stop;
            current->stop_signal = (uint8_t)kernel_ctz(pending_stop);
            proc_stop(current);
            // Execution resumes here after SIGCONT.
            // Re-evaluate signals — new ones may have arrived while stopped.
        }

        // Clear any pending signals whose disposition is SIG_IGN, and also
        // default-ignore signals (SIGCHLD) when handler is still SIG_DFL.
        // SIGKILL (bit 9) can never be ignored even if handlers[9] == SIG_IGN.
        {
            uint32_t tmp = current->pending_signals & ~current->signal_mask;
            while (tmp) {
                int s = kernel_ctz((uint32_t)(tmp));
                tmp &= tmp - 1;
                if (s == KERNEL_SIGKILL || s == KERNEL_SIGSTOP)
                    continue;
                if (current->handlers[s] == (void *)SIG_IGN ||
                    ((1U << s) & KERNEL_DFL_IGNORE_SIGS &&
                     current->handlers[s] == (void *)SIG_DFL))
                    current->pending_signals &= ~(1u << s);
            }
        }

        /* Dispatch one pending, unmasked custom handler per syscall return.
           Only SIGKILL is truly uncatchable; SIGSTOP is already excluded by
           KERNEL_STOP_SIGS.  Signals with a fatal default (SIGUSR1, SIGTERM,
           etc.) reach this block when a custom handler is installed — the
           fatal check below only fires for SIG_DFL-disposition signals. */
        {
            uint32_t catchable = current->pending_signals
                                & ~current->signal_mask
                                & ~(1U << KERNEL_SIGKILL)
                                & ~KERNEL_STOP_SIGS;
            while (catchable) {
                int s = kernel_ctz((uint32_t)catchable);
                catchable &= catchable - 1;
                if ((uintptr_t)current->handlers[s] > (uintptr_t)SIG_IGN) {
                    signal_dispatch(frame, s);
                    current->pending_signals &= ~(1u << s);
                    if (!(current->sig_flags[s] & SA_NODEFER))
                        current->signal_mask |= (1u << s) | current->sa_masks[s];
                    break;
                }
            }
        }

        // Check if this process has a fatal pending signal before handling anything.
        // This catches processes that were signaled while running (not sleeping).
        // Fatal signals: filtered by signal_mask (except SIGKILL which can't be masked,
        // but we enforce that in sys_sigprocmask, so signal_mask bit 9 is always 0)
        uint32_t unmasked = current->pending_signals & ~current->signal_mask;
        if (unmasked & KERNEL_FATAL_SIGS) {
            current->pending_signals = 0;
            sys_exit(-kernel_ctz((uint32_t)(unmasked & KERNEL_FATAL_SIGS)));
            return; // unreachable
        }
    }

    // System call number is in a7
    const uint64_t syscall_num = frame->a7;

    // Arguments are in a0-a5
    const uint64_t arg0 = frame->a0;
    const uint64_t arg1 = frame->a1;
    const uint64_t arg2 = frame->a2;
    const uint64_t arg3 = frame->a3;
    const uint64_t arg4 = frame->a4;
    const uint64_t arg5 = frame->a5;

    // exec rewrites the entire trapframe on success; handle before the
    // generic ret path so frame->a0 is not overwritten afterwards.
    if (syscall_num == SYS_exec) {
        char kpath[KARG_MAX];
        char *kargv[MAX_ARGS + 1];
        char *kenvp[MAX_ARGS + 1];
        int   argc = 0, envc = 0;

        for (int i = 0; i <= MAX_ARGS; i++) { kargv[i] = NULL; kenvp[i] = NULL; }

        set_csr_bit(sstatus, SSTATUS_SUM);

        /* Copy path */
        strncpy(kpath, (const char *)arg0, sizeof(kpath) - 1);
        kpath[sizeof(kpath) - 1] = '\0';

        /* Copy argv strings from userspace */
        char **uargv = (char **)arg1;
        while (uargv && argc < MAX_ARGS - 1) {
            char *uarg = uargv[argc];
            if (!uarg) break;
            if ((uint64_t)uarg >= KERNBASE) break;
            kargv[argc] = (char *)kmalloc(KARG_MAX);
            if (!kargv[argc]) {
                clear_csr_bit(sstatus, SSTATUS_SUM);
                for (int i = 0; i < argc; i++) kfree(kargv[i]);
                frame->a0 = (uint64_t)(int64_t)(-ENOMEM);
                return;
            }
            strncpy(kargv[argc], uarg, KARG_MAX - 1);
            /* E2BIG: string was truncated — it has >= KARG_MAX-1 characters */
            if (kargv[argc][KARG_MAX - 2] != '\0') {
                clear_csr_bit(sstatus, SSTATUS_SUM);
                kfree(kargv[argc]);
                for (int i = 0; i < argc; i++) kfree(kargv[i]);
                frame->a0 = (uint64_t)(int64_t)(-E2BIG);
                return;
            }
            kargv[argc][KARG_MAX - 1] = '\0';
            argc++;
        }
        /* E2BIG: more args exist beyond the MAX_ARGS-1 limit */
        if (argc == MAX_ARGS - 1 && uargv) {
            char *extra = uargv[argc];
            if (extra && (uint64_t)extra < KERNBASE) {
                clear_csr_bit(sstatus, SSTATUS_SUM);
                for (int i = 0; i < argc; i++) kfree(kargv[i]);
                frame->a0 = (uint64_t)(int64_t)(-E2BIG);
                return;
            }
        }
        kargv[argc] = NULL;

        /* Copy envp strings from userspace (arg2 = frame->a2) */
        char **uenvp = (char **)arg2;
        while (uenvp && envc < MAX_ARGS - 1) {
            char *uenv = uenvp[envc];
            if (!uenv) break;
            if ((uint64_t)uenv >= KERNBASE) break;
            kenvp[envc] = (char *)kmalloc(KARG_MAX);
            if (!kenvp[envc]) {
                clear_csr_bit(sstatus, SSTATUS_SUM);
                for (int i = 0; i < argc; i++) kfree(kargv[i]);
                for (int i = 0; i < envc; i++) kfree(kenvp[i]);
                frame->a0 = (uint64_t)(int64_t)(-ENOMEM);
                return;
            }
            strncpy(kenvp[envc], uenv, KARG_MAX - 1);
            /* E2BIG: string was truncated */
            if (kenvp[envc][KARG_MAX - 2] != '\0') {
                clear_csr_bit(sstatus, SSTATUS_SUM);
                kfree(kenvp[envc]);
                for (int i = 0; i < argc; i++) kfree(kargv[i]);
                for (int i = 0; i < envc; i++) kfree(kenvp[i]);
                frame->a0 = (uint64_t)(int64_t)(-E2BIG);
                return;
            }
            kenvp[envc][KARG_MAX - 1] = '\0';
            envc++;
        }
        /* E2BIG: more env vars exist beyond the MAX_ARGS-1 limit */
        if (envc == MAX_ARGS - 1 && uenvp) {
            char *extra = uenvp[envc];
            if (extra && (uint64_t)extra < KERNBASE) {
                clear_csr_bit(sstatus, SSTATUS_SUM);
                for (int i = 0; i < argc; i++) kfree(kargv[i]);
                for (int i = 0; i < envc; i++) kfree(kenvp[i]);
                frame->a0 = (uint64_t)(int64_t)(-E2BIG);
                return;
            }
        }
        kenvp[envc] = NULL;

        clear_csr_bit(sstatus, SSTATUS_SUM);

        int rc = exec(kpath, kargv, kenvp);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);

        if (rc < 0) {
            printk("[exec] exec(%s) failed rc=%d argc=%d\n", kpath, rc, argc);
            frame->a0 = (uint64_t)(int64_t)rc;   /* -ENOENT, -EACCES, -ENOEXEC, etc. */
        } else {
            // Update process name to the basename of the executed binary
            const char *base = kpath;
            for (const char *p = kpath; *p; p++)
                if (*p == '/') base = p + 1;
            strncpy(current->name, base, MAX_NAME - 1);
            current->name[MAX_NAME - 1] = '\0';
        }
        return;
    }

    /* sys_sigreturn restores a0 from the sigframe; handle before the generic
       ret path to prevent frame->a0 = ret from overwriting the restored value. */
    if (syscall_num == SYS_sigreturn) {
        sys_sigreturn(frame);
        return;
    }

    uint64_t ret = 0;

    switch (syscall_num) {
        case SYS_exit:
            sys_exit((int) arg0);
            break;

        case SYS_fork:
            ret = sys_fork();
            break;

        case SYS_read:
            ret = sys_read((int) arg0, (void *) arg1, (size_t) arg2);
            break;

        case SYS_write:
            ret = sys_write((int) arg0, (const void *) arg1, (size_t) arg2);
            break;

        case SYS_open:
            ret = sys_open((const char *) arg0, (int) arg1, (mode_t) arg2);
            break;

        case SYS_close:
            ret = sys_close((int) arg0);
            break;

        case SYS_wait:
            ret = sys_wait((int *) arg0);
            break;

        // case SYS_exec:
        //     ret = sys_exec((const char *) arg0, (char **) arg1);
        //     break;

        case SYS_getpid:
            ret = sys_getpid();
            break;

        case SYS_getppid:
            ret = sys_getppid();
            break;

        case SYS_sbrk:
            ret = (uint64_t) sys_sbrk((intptr_t) arg0);
            break;

        case SYS_sleep:
            ret = sys_sleep((int) arg0);
            break;

        case SYS_uptime:
            ret = sys_uptime();
            break;

        case SYS_kill:
            ret = sys_kill((pid_t) arg0, (int) arg1);
            break;

        case SYS_mmap:
            ret = (uint64_t) sys_mmap((void *) arg0, (size_t) arg1, (int) arg2,
                                      (int) arg3, (int) arg4, (off_t) arg5);
            break;

        case SYS_munmap:
            ret = sys_munmap((void *) arg0, (size_t) arg1);
            break;

        case SYS_brk:
            ret = sys_brk((void *) arg0);
            break;

        case SYS_lseek:
            ret = (uint64_t)(int64_t)sys_lseek((int) arg0, (off_t) arg1, (int) arg2);
            break;

        case SYS_unlink:
            ret = sys_unlink((const char *) arg0);
            break;

        case SYS_link:
            ret = sys_link((const char *)arg0, (const char *)arg1);
            break;

        case SYS_symlink:
            ret = sys_symlink((const char *)arg0, (const char *)arg1);
            break;

        case SYS_readlink:
            ret = (uint64_t)(int64_t)sys_readlink(
                      (const char *)arg0, (char *)arg1, (size_t)arg2);
            break;

        case SYS_mkdir:
            ret = sys_mkdir((const char *) arg0, (mode_t) arg1);
            break;

        case SYS_sync:
            ret = sys_sync();
            break;

        case SYS_pipe:
            ret = sys_pipe((int *) arg0);
            break;

        case SYS_dup:
            ret = sys_dup((int) arg0);
            break;

        case SYS_fcntl:
            ret = sys_fcntl((int)arg0, (int)arg1, (long)arg2);
            break;

        case SYS_dup2:
            ret = sys_dup2((int) arg0, (int) arg1);
            break;

        case SYS_tcgetattr:
            ret = sys_tcgetattr((int)arg0, (struct termios *)arg1);
            break;

        case SYS_tcsetattr:
            ret = sys_tcsetattr((int)arg0, (int)arg1, (const struct termios *)arg2);
            break;

        case SYS_waitpid:
            ret = sys_waitpid((pid_t) arg0, (int *) arg1, (int) arg2);
            break;

        case SYS_sigprocmask:
            ret = sys_sigprocmask((int)arg0, (uint32_t *)arg1, (uint32_t *)arg2);
            break;

        case SYS_chdir:
            ret = (uint64_t)sys_chdir((const char *)arg0);
            break;

        case SYS_getcwd:
            ret = (uint64_t)sys_getcwd((char *)arg0, (size_t)arg1);
            break;

        case SYS_fs_getdents:
            ret = sys_fs_getdents((int)arg0, (void *)arg1, (uint32_t)arg2);
            break;

        case SYS_getrusage:
            ret = sys_getrusage((int)arg0, (struct rusage *)arg1);
            break;

        case SYS_gettimeofday:
            ret = sys_gettimeofday((struct timeval_k *)arg0, (void *)arg1);
            break;

        case SYS_nice:
            ret = sys_nice((int)arg0);
            break;

        case SYS_yield:
            ret = sys_yield();
            break;

        case SYS_pause:
            ret = sys_pause();
            break;

        case SYS_procinfo:
            ret = sys_procinfo((struct proc_info *) arg0, (int) arg1);
            break;

        case SYS_stat:
            ret = sys_stat((char *)arg0, (void *)arg1);
            break;

        case SYS_fstat:
            ret = sys_fstat((int)arg0, (void *)arg1);
            break;

        case SYS_lstat:
            ret = sys_lstat((const char *)arg0, (void *)arg1);
            break;

        case SYS_access:
            ret = sys_access((const char *)arg0, (int)arg1);
            break;

        case SYS_fstatat:
            ret = sys_fstatat((int)arg0, (const char *)arg1, (struct stat *)arg2, (int)arg3);
            break;

        case SYS_chmod:
            ret = sys_chmod((const char *)arg0, (mode_t)arg1);
            break;

        case SYS_fchmod:
            ret = sys_fchmod((int)arg0, (mode_t)arg1);
            break;

        case SYS_rmdir:
            ret = sys_rmdir((const char *)arg0);
            break;

        case SYS_rename:
            ret = sys_rename((const char *)arg0, (const char *)arg1);
            break;

        case SYS_alarm:
            ret = sys_alarm((unsigned int) arg0);
            break;

        case SYS_sigaction:
            ret = sys_sigaction((int)arg0,
                                (const struct sigaction *)arg1,
                                (struct sigaction *)arg2);
            break;

        case SYS_sigpending:
            ret = sys_sigpending((uint32_t *)arg0);
            break;

        case SYS_sigsuspend:
            ret = sys_sigsuspend((uint32_t *)arg0);
            break;

        case SYS_getpgrp:
            ret = sys_getpgrp();
            break;

        case SYS_setpgid:
            ret = sys_setpgid((pid_t)arg0, (pid_t)arg1);
            break;

        case SYS_setsid:
            ret = sys_setsid();
            break;

        case SYS_getpgid:
            ret = sys_getpgid((pid_t)arg0);
            break;

        case SYS_getsid:
            ret = sys_getsid((pid_t)arg0);
            break;

        case SYS_tcsetpgrp:
            ret = sys_tcsetpgrp((pid_t)arg1);
            break;

        case SYS_tcgetpgrp:
            ret = sys_tcgetpgrp();
            break;

        case SYS_nanosleep:
            ret = sys_nanosleep((const struct timespec *)arg0,
                                (struct timespec *)arg1);
            break;

        case SYS_clock_nanosleep:
            ret = sys_clock_nanosleep((clockid_t)arg0, (int)arg1,
                                      (const struct timespec *)arg2,
                                      (struct timespec *)arg3);
            break;

        case SYS_getitimer:
            ret = sys_getitimer((int)arg0, (struct itimerval_k *)arg1);
            break;

        case SYS_setitimer:
            ret = sys_setitimer((int)arg0, (const struct itimerval_k *)arg1,
                                (struct itimerval_k *)arg2);
            break;

        case SYS_sysconf:
            ret = sys_sysconf((int)arg0);
            break;

        /* Single-user kernel: all processes run as UID/GID 0 (root). */
        case SYS_getuid:
        case SYS_geteuid:
            ret = 0;
            break;

        case SYS_getgid:
        case SYS_getegid:
            ret = 0;
            break;

        case SYS_fsync:
            ret = sys_fsync((int)arg0);
            break;

        case SYS_fdatasync:
            ret = sys_fdatasync((int)arg0);
            break;

        case SYS_getrlimit:
            ret = sys_getrlimit((int)arg0, (struct rlimit *)arg1);
            break;

        case SYS_setrlimit:
            ret = sys_setrlimit((int)arg0, (const struct rlimit *)arg1);
            break;

        case SYS_pathconf:
            ret = sys_pathconf((const char *)arg0, (int)arg1);
            break;
        
        case SYS_select:
            ret = sys_select((int)arg0, (k_fd_set *)arg1, (k_fd_set *)arg2,
                             (k_fd_set *)arg3, (struct timeval_k *)arg4);
            break;

        case SYS_fpathconf:
            ret = sys_fpathconf((int)arg0, (int)arg1);
            break;

        case SYS_uname:
            ret = sys_uname((void *)arg0);
            break;

        default:
            // Unknown syscall number from user space will be recognized as delivering SIGSYS (Bad system call)
            // Set a0 = -ENOSYS so masked SIGSYS callers see a defined return value.
            // If SIGSYS is unmasked (it is maskable), trap_handler fires proc_exit(-31)
            // before this function returns to user mode.
            if (current) {
                current->pending_signals |= (1U << KERNEL_SIGSYS);
                ret = -ENOSYS;
            }
            else {
                printk("Unknown system call: %d (no current process)\n", syscall_num);
                ret = -1;
            }
            break;
    }

    // Return value goes in a0
    frame->a0 = ret;
}

// System call implementations (stubs for now)

void sys_exit(int status) {
    proc_exit(status);
}

pid_t sys_fork(void) {
    if (!current) return -1;

    /* Count live (non-ZOMBIE, non-UNUSED) processes to enforce RLIMIT_NPROC. */
    /* Note: this enforces a system-wide count. POSIX defines `RLIMIT_NPROC` per real user ID,
        but since SBUnix has no multi-user support, a system-wide count is a reasonable
        approximation and exercises the enforcement path. */
    int live = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        struct proc *p = &proctab[i];
        if (p->state != PROC_UNUSED && p->state != PROC_ZOMBIE) live++;
    }
    if ((uint64_t)live >= current->rlimits[RLIMIT_NPROC].rlim_cur)
        return -EAGAIN;

    // Allocate and initialize new process (assigns pid, stack, context)
    struct proc *child = proc_create(current->name);
    if (!child) return -ENOMEM;

    // Inherit parent's open files (including stdio) and bump file refs.
    for (int i = 0; i < MAX_FDS; i++) {
        child->fds.files[i] = current->fds.files[i];
        child->fds.fd_flags[i] = current->fds.fd_flags[i];
        if (child->fds.files[i])
            file_ref(child->fds.files[i]);  // bump refcount if vfs supports it
    }

    // Copy parent's address space (page table + VMA list)
    child->pagetable = vmm_copy_pagetable(current->pagetable);
    if (!child->pagetable) {
        proc_free(child);
        return -ENOMEM;
    }
    if (vma_copy_all(&child->vma_list, current->vma_list) != 0) {
        vmm_free_pagetable(child->pagetable);
        proc_free(child);
        return -ENOMEM;
    }

    child->sz = current->sz;
    child->signal_mask = current->signal_mask; // Fork child inherits parent's signal mask
    // POSIX: child inherits parent's signal disposition table across fork
    memcpy(child->handlers, current->handlers, sizeof(current->handlers));
    memcpy(child->sig_flags, current->sig_flags, sizeof(current->sig_flags));
    memcpy(child->sa_masks,    current->sa_masks,    sizeof(current->sa_masks));
    memcpy(child->sa_restorer, current->sa_restorer, sizeof(current->sa_restorer));
    child->priority    = current->priority;    /* POSIX: nice value is inherited across fork */
    // Child also inherits parent's process group and session IDs
    child->pgid = current->pgid;
    child->sid = current->sid;

    /* Child inherits parent's resource limits (POSIX). */
    memcpy(child->rlimits, current->rlimits, sizeof(current->rlimits));
    memcpy(child->exe_path, current->exe_path, sizeof(current->exe_path));

    /* Child starts one level below the parent so a timer-preempted parent
     * re-enters at the same level as the child; LIFO insertion then keeps
     * the parent at the head of that level so it finishes forking remaining
     * children before any child gets scheduled.  Clamped to MLFQ_LEVELS-1. */
    child->mlfq_level  = (current->mlfq_level < MLFQ_LEVELS - 1)
                         ? current->mlfq_level + 1
                         : MLFQ_LEVELS - 1;
    child->fs = fs_struct_copy(current->fs);
    if (!child->fs) {
        vma_free_all(child->vma_list);
        vmm_free_pagetable(child->pagetable);
        proc_free(child);
        return -ENOMEM;
    }

    // Copy parent's trap frame and register state to the child
    if (current->trapframe) {
        child->trapframe = (trap_frame *)kmalloc(sizeof(trap_frame));
        if (!child->trapframe) {
            vmm_free_pagetable(child->pagetable);
            proc_free(child);
            return -ENOMEM;
        }
        child->trapframe_owned = 1;
        memcpy(child->trapframe, current->trapframe, sizeof(trap_frame));

        // Child returns 0 from fork
        child->trapframe->a0 = 0;

        // sepc was captured pointing AT the ecall instruction.
        // handle_exception adds +4 for the parent after syscall_handler returns,
        // so we must advance it here for the child.
        child->trapframe->sepc += 4;

        // Route child's first kernel-side wakeup through fork_ret, not proc_entry.
        // proc_entry would jump to the ELF entry point (current->pc); fork_ret
        // restores the full trapframe and srets to the instruction after ecall.
        child->context.ra = (uint64_t)fork_ret;

    } else {
        child->trapframe = NULL;
        child->trapframe_owned = 0;
    }
    child->pc = current->pc;
    child->sp = current->sp;
    memcpy(child->regs, current->regs, sizeof(current->regs));
    child->satp = current->satp;

    // Parent-child linking
    child->parent = current;
    child->sibling = current->first_child;
    current->first_child = child;

    // Child is now fully initialized and safe to run.
    proc_ready(child);

    // Return child pid to parent that called fork syscall
    return child->pid;
}

/* Bounce-buffer size for sys_read / sys_write.
 * Must be well below KSTACK_SIZE (4096): leaves room for the trap frame,
 * syscall_handler frame, and this function's own frame. */
#define SYSCALL_BUF_SZ 256

ssize_t sys_read(int fd, void *buf, size_t count) {
    if (!current || !buf)
        return -EINVAL;
    if (count == 0)
        return 0;

    char kbuf[SYSCALL_BUF_SZ];
    char *dst = (char *)buf;
    ssize_t total = 0;
    while ((size_t)total < count) {
        size_t chunk = count - (size_t)total;
        if (chunk > SYSCALL_BUF_SZ) chunk = SYSCALL_BUF_SZ;
        int n = file_read(fd, kbuf, chunk);
        if (n <= 0) return total > 0 ? total : (ssize_t)n;
        if (copy_to_user(dst + total, kbuf, (size_t)n) < 0) return -EFAULT;
        total += n;
        if ((size_t)n < chunk) break;
    }
    return total;
}

ssize_t sys_write(int fd, const void *buf, size_t count) {
    if (!current || !buf)
        return -EINVAL;
    if (count == 0)
        return 0;

    char kbuf[SYSCALL_BUF_SZ];
    const char *src = (const char *)buf;
    ssize_t total = 0;
    while ((size_t)total < count) {
        size_t chunk = count - (size_t)total;
        if (chunk > SYSCALL_BUF_SZ) chunk = SYSCALL_BUF_SZ;
        if (copy_from_user(kbuf, src + total, chunk) < 0) return -EFAULT;
        int n = file_write(fd, kbuf, chunk);
        if (n <= 0) return total > 0 ? total : (ssize_t)n;
        total += n;
    }
    return total;
}

// int sys_open(const char *path, int flags) {
//     (void) flags;
//     if (!current || !path) return -EINVAL;
//
//     char kpath[MAX_PATH];
//     strncpy_from_user(kpath, path, MAX_PATH - 1);
//     kpath[MAX_PATH - 1] = '\0';
//
//     return file_open(kpath);
// }

int sys_open(const char *path, int flags, mode_t mode) {
    if (!current || !path) return -EINVAL;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, path, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';
    int ret = file_open_with_flags(kpath, flags, mode);
    kfree(kpath);
    return ret;
}

int sys_close(int fd) {
    return file_close(fd);
}

int sys_pipe(int pipefd[2]) {
    if (!current || !pipefd) return -EFAULT;

    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!p) return -ENOMEM;
    memset(p, 0, sizeof(*p));
    p->readers = 1;
    p->writers = 1;

    file_t *read_end = (file_t *)kmalloc(sizeof(file_t));
    if (!read_end) {
        kfree(p);
        return -ENOMEM;
    }
    memset(read_end, 0, sizeof(*read_end));
    read_end->type = FILE_PIPE;
    read_end->pipe = p;
    read_end->pipe_flags = PIPE_END_READ;
    read_end->ref_count = 1;

    file_t *write_end = (file_t *)kmalloc(sizeof(file_t));
    if (!write_end) {
        kfree(read_end);
        kfree(p);
        return -ENOMEM;
    }
    memset(write_end, 0, sizeof(*write_end));
    write_end->type = FILE_PIPE;
    write_end->pipe = p;
    write_end->pipe_flags = PIPE_END_WRITE;
    write_end->ref_count = 1;

    int read_fd = fd_alloc(&current->fds, read_end);
    if (read_fd < 0) {
        kfree(write_end);
        kfree(read_end);
        kfree(p);
        return -EMFILE;
    }

    int write_fd = fd_alloc(&current->fds, write_end);
    if (write_fd < 0) {
        current->fds.files[read_fd] = NULL;
        current->fds.fd_flags[read_fd] = 0;
        kfree(write_end);
        kfree(read_end);
        kfree(p);
        return -EMFILE;
    }

    int kfds[2] = { read_fd, write_fd };
    if (copy_to_user(pipefd, kfds, sizeof(kfds)) < 0) {
        fd_close(&current->fds, read_fd);
        fd_close(&current->fds, write_fd);
        return -EFAULT;
    }
    return 0;
}

int sys_dup(int oldfd) {
    if (!current) return -EBADF;

    file_t *f = f_get(current, oldfd);
    if (!f) return -EBADF;

    int newfd = fd_alloc(&current->fds, f);
    if (newfd < 0) {
        f_put(f);
        return -EMFILE;
    }

    current->fds.fd_flags[newfd] = 0;
    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    if (!current) return -EBADF;
    if (newfd < 0 || newfd >= MAX_FDS) return -EBADF;

    file_t *f = f_get(current, oldfd);
    if (!f) return -EBADF;

    if (oldfd == newfd) {
        f_put(f);
        return newfd;
    }

    if (fd_get(&current->fds, newfd)) {
        fd_close(&current->fds, newfd);
    }

    current->fds.files[newfd] = f;
    current->fds.fd_flags[newfd] = 0;
    return newfd;
}

int sys_fcntl(int fd, int cmd, long arg) {
    if (!current || fd < 0 || fd >= MAX_FDS)
        return -EBADF;

    file_t *f = fd_get(&current->fds, fd);
    if (!f)
        return -EBADF;

    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC: {
            if (arg < 0 || arg >= MAX_FDS)
                return -EINVAL;
            file_ref(f);
            int newfd = fd_alloc_from(&current->fds, f, (int)arg);
            if (newfd < 0) {
                f_put(f);
                return newfd;
            }
            current->fds.fd_flags[newfd] = (cmd == F_DUPFD_CLOEXEC) ? FD_CLOEXEC : 0;
            return newfd;
        }

        case F_GETFD:
            return current->fds.fd_flags[fd] & FD_CLOEXEC;

        case F_SETFD:
            current->fds.fd_flags[fd] = ((int)arg & FD_CLOEXEC) ? FD_CLOEXEC : 0;
            return 0;

        case F_GETFL:
            return f->flags & ~O_CLOEXEC;

        case F_SETFL: {
            int settable = O_APPEND | O_NONBLOCK;
            f->flags = (f->flags & ~settable) | ((int)arg & settable);
            return 0;
        }

        default:
            return -EINVAL;
    }
}

int sys_tcgetattr(int fd, struct termios *termios_p) {
    if (!current || !termios_p)
        return -EINVAL;

    file_t *f = f_get(current, fd);
    if (!f)
        return -EBADF;

    if (f->type != FILE_STDIN && f->type != FILE_STDOUT) {
        f_put(f);
        return -ENOTTY;
    }

    struct termios kt;
    int rc = term_tcgetattr(&console_term, &kt);
    if (rc == 0)
        copy_to_user(termios_p, &kt, sizeof(kt));

    f_put(f);
    return rc;
}

int sys_tcsetattr(int fd, int action, const struct termios *termios_p) {
    if (!current || !termios_p)
        return -EINVAL;

    file_t *f = f_get(current, fd);
    if (!f)
        return -EBADF;

    if (f->type != FILE_STDIN && f->type != FILE_STDOUT) {
        f_put(f);
        return -ENOTTY;
    }

    struct termios kt;
    copy_from_user(&kt, termios_p, sizeof(kt));
    int rc = term_tcsetattr(&console_term, action, &kt);

    f_put(f);
    return rc;
}

pid_t sys_wait(int *status) {
    // Use the new proc_wait implementation for process reaping and blocking
    return proc_wait(status);
}

/* ------------------------------------------------------------------ */
/* sys_select                                                           */
/* ------------------------------------------------------------------ */

/*
 * Minimal POSIX select for SBUnix:
 *
 * readfds:   stdin (FILE_STDIN) blocks until term_has_input(); all other fds
 *            (regular files, pipes, stdout) are always considered readable.
 * writefds:  all fds are always writable — SBUnix has no non-blocking writes.
 * exceptfds: ignored (no OOB data in this kernel).
 * timeout:   NULL = infinite wait; {0,0} = non-blocking poll; else timed wait.
 *
 * Blocking strategy:
 *   - Set console_term.select_waiter = current so the TTY ISR (term_wake)
 *     calls proc_wakeup(current) when a line becomes available.
 *   - Use current->wake_tick for timer-based wakeup (same as nanosleep).
 *   - Sleep on (current) so both wakeup paths use the same channel.
 */
int sys_select(int nfds, k_fd_set *urfds, k_fd_set *uwfds, k_fd_set *uefds,
               struct timeval_k *utv)
{
    if (!current) return -EINVAL;
    if (nfds < 0 || nfds > KSELECT_SETSIZE) return -EINVAL;
    (void)uefds;  /* exceptfds not supported */

    k_fd_set krf, kwf;
    int has_rf = (urfds != NULL);
    int has_wf = (uwfds != NULL);

    KFD_ZERO(&krf);
    KFD_ZERO(&kwf);
    if (has_rf) copy_from_user(&krf, urfds, sizeof(k_fd_set));
    if (has_wf) copy_from_user(&kwf, uwfds, sizeof(k_fd_set));

    /* Parse timeout */
    int infinite = (utv == NULL);
    int nonblock = 0;
    uint64_t deadline = 0;

    if (utv) {
        struct timeval_k ktv;
        copy_from_user(&ktv, utv, sizeof(struct timeval_k));
        if (ktv.tv_sec == 0 && ktv.tv_usec == 0) {
            nonblock = 1;
        } else {
            uint64_t tks = (uint64_t)ktv.tv_sec * TICKS_PER_SEC +
                           ((uint64_t)ktv.tv_usec * TICKS_PER_SEC + 999999ULL) / 1000000ULL;
            if (tks == 0) tks = 1;  /* round up to at least 1 tick */
            deadline = timer_ticks + tks;
        }
    }

    /* Determine whether stdin appears in rfds (needs ISR-based wakeup) */
    int stdin_selected = 0;
    if (has_rf) {
        for (int i = 0; i < nfds; i++) {
            if (!KFD_ISSET(i, &krf)) continue;
            file_t *f = f_get(current, i);
            if (!f) continue;
            int is_stdin = (f->type == FILE_STDIN);
            f_put(f);
            if (is_stdin) { stdin_selected = 1; break; }
        }
    }

    for (;;) {
        k_fd_set out_rf, out_wf;
        KFD_ZERO(&out_rf);
        KFD_ZERO(&out_wf);
        int ready = 0;

        /* Evaluate readfds */
        if (has_rf) {
            for (int i = 0; i < nfds; i++) {
                if (!KFD_ISSET(i, &krf)) continue;
                file_t *f = f_get(current, i);
                if (!f) continue;
                int readable = (f->type == FILE_STDIN)
                               ? term_has_input(&console_term) : 1;
                f_put(f);
                if (readable) { KFD_SET(i, &out_rf); ready++; }
            }
        }

        /* Evaluate writefds — all fds always writable in SBUnix */
        if (has_wf) {
            for (int i = 0; i < nfds; i++) {
                if (KFD_ISSET(i, &kwf)) { KFD_SET(i, &out_wf); ready++; }
            }
        }

        /* Check for signal interruption */
        if (current->pending_signals & ~current->signal_mask)
            return -EINTR;

        /* Return if ready, non-blocking, or deadline passed */
        if (ready > 0 || nonblock || (!infinite && timer_ticks >= deadline)) {
            if (urfds) copy_to_user(urfds, &out_rf, sizeof(k_fd_set));
            if (uwfds) copy_to_user(uwfds, &out_wf, sizeof(k_fd_set));
            return ready;
        }

        /* Sleep until stdin has data, timeout fires, or a signal arrives */
        if (stdin_selected)
            console_term.select_waiter = current;
        if (!infinite)
            current->wake_tick = deadline;

        proc_sleep(current);

        /* Clear waiter in case we were woken by timer or signal (not ISR) */
        console_term.select_waiter = NULL;

        if (!infinite && current->wake_tick == 0) {
            /* Timer expired cleanly — return 0 (no fds ready) */
            if (urfds) copy_to_user(urfds, &out_rf, sizeof(k_fd_set));
            if (uwfds) copy_to_user(uwfds, &out_wf, sizeof(k_fd_set));
            return 0;
        }
        current->wake_tick = 0;
        /* Loop: re-evaluate fds (may have been woken by ISR or signal) */
    }
}

pid_t sys_waitpid(pid_t pid, int *status, int options) {
    return proc_waitpid(pid, status, options);
}

pid_t sys_getpid(void) {
    struct proc *p = proc_current();
    if (p != NULL) {
        return p->pid;
    }
    return -1;
}

pid_t sys_getppid(void) {
    struct proc *p = proc_current();
    if (p != NULL && p->parent != NULL) {
        return p->parent->pid;
    }
    return 0;   /* POSIX: init (PID 1) has no parent → return 0 */
}

int sys_sleep(int seconds) {
    if (seconds <= 0) return 0;
    uint64_t wake_at = timer_ticks + (uint64_t)seconds * TICKS_PER_SEC;
    // Loop: if woken early by stop/continue (SIGTSTP+SIGCONT), go back to
    // sleep for the remaining time.  Fatal signals (SIGINT, SIGKILL, …) are
    // handled inside proc_sleep (calls proc_exit), so we never loop on those.
    do {
        current->wake_tick = wake_at;
        proc_sleep(current);   // sleep on self; timer_handler calls proc_wakeup(p)
        current->wake_tick = 0;
    } while (timer_ticks < wake_at);
    return 0;
}

int sys_uptime(void) {
    return (int)timer_ticks;
}

int sys_kill(pid_t pid, int sig) {
    if (sig < 0 || sig > 31) return -EINVAL;

    // POSIX: kill(-1, sig) broadcasts to all processes except init (PID 1) and the caller.
    // Returns -1 (ESRCH) if no eligible targets exist.
    if (pid == -1) {
        if (sig == 0) return 0;  // validity check — always succeeds
        int sent = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            // Skip: not yet live, already dead, init, or self.
            // PROC_CREATED is excluded: the process has no signal context yet
            // (no trapframe, no user page table, not yet scheduled).
            if (p->state == PROC_UNUSED ||
                p->state == PROC_ZOMBIE  ||
                p->state == PROC_CREATED) continue;
            if (p->pid == 1 || p == current) continue;
            sent++;
            if (sig == KERNEL_SIGCONT) {
                p->pending_signals &= ~KERNEL_STOP_SIGS;
                if (p->state == PROC_STOPPED)
                    proc_unstop(p);
            } else if ((1U << sig) & KERNEL_STOP_SIGS) {
                // POSIX: SIG_IGN discards catchable stop signals entirely
                if (sig != KERNEL_SIGSTOP && p->handlers[sig] == (void *)1 /* SIG_IGN */)
                    continue;
                if (sig != KERNEL_SIGSTOP && (p->signal_mask & (1U << sig))) {
                    p->pending_signals |= (1U << sig);
                } else {
                    p->pending_signals &= ~(1U << KERNEL_SIGCONT);
                    if (p->state == PROC_READY || p->state == PROC_BLOCKED) {
                        p->stop_signal = (uint8_t)sig;
                        proc_stop(p);
                    } else
                        p->pending_signals |= (1U << sig);
                }
            } else {
                p->pending_signals |= (1U << sig);
                if (p->state == PROC_BLOCKED &&
                    (sig == KERNEL_SIGKILL || !(p->signal_mask & (1U << sig))))
                    proc_unblock(p);
            }
        }
        return (sent > 0) ? 0 : -ESRCH;
    }

    /* POSIX: kill(0, sig) — send to every process in the caller's process group.
     * POSIX: kill(-pgid, sig) — send to every process in process group pgid. */
    if (pid == 0 || pid < -1) {
        /* kill(0, sig) requires a current process to determine the group. */
        if (pid == 0 && current == NULL) return -ESRCH;
        pid_t target_pgid = (pid == 0) ? current->pgid : (pid_t)(-pid);
        if (sig == 0) {
            /* Validity check: succeed if any live process in the group exists */
            for (int i = 0; i < MAX_PROCS; i++) {
                if (proctab[i].state != PROC_UNUSED &&
                    proctab[i].state != PROC_ZOMBIE &&
                    proctab[i].pgid == target_pgid)
                    return 0;
            }
            return -ESRCH;
        }
        int sent = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            if (p->state == PROC_UNUSED ||
                p->state == PROC_ZOMBIE  ||
                p->state == PROC_CREATED) continue;
            if (p->pgid != target_pgid) continue;
            sent++;
            if (sig == KERNEL_SIGCONT) {
                p->pending_signals &= ~KERNEL_STOP_SIGS;
                if (p->state == PROC_STOPPED)
                    proc_unstop(p);
            } else if ((1U << sig) & KERNEL_STOP_SIGS) {
                // POSIX: SIG_IGN discards catchable stop signals entirely
                if (sig != KERNEL_SIGSTOP && p->handlers[sig] == (void *)1 /* SIG_IGN */)
                    continue;
                if (sig != KERNEL_SIGSTOP && (p->signal_mask & (1U << sig))) {
                    p->pending_signals |= (1U << sig);
                } else {
                    p->pending_signals &= ~(1U << KERNEL_SIGCONT);
                    if (p->state == PROC_READY || p->state == PROC_BLOCKED) {
                        p->stop_signal = (uint8_t)sig;
                        proc_stop(p);
                    } else
                        p->pending_signals |= (1U << sig);
                }
            } else {
                p->pending_signals |= (1U << sig);
                if (p->state == PROC_BLOCKED &&
                    (sig == KERNEL_SIGKILL || !(p->signal_mask & (1U << sig))))
                    proc_unblock(p);
            }
        }
        return (sent > 0) ? 0 : -ESRCH;
    }

    struct proc *target = proc_find(pid);
    if (!target || target->state == PROC_UNUSED || target->state == PROC_ZOMBIE)
        return -ESRCH;

    // sig == 0: validity check only (POSIX), no signal sent
    if (sig == 0) return 0;
    
    // SIGCONT: resume a stopped process and clear any pending SIGSTOP
    if (sig == SIGCONT) {
        target->pending_signals &= ~KERNEL_STOP_SIGS;
        if (target->state == PROC_STOPPED)
            proc_unstop(target);
        return 0;
    }

    // Stop signals: act immediately when possible.
    // SIGSTOP is unmaskable (POSIX); SIGTSTP/SIGTTIN/SIGTTOU respect signal_mask.
    // POSIX: SIG_IGN discards catchable stop signals entirely.
    if ((1U << sig) & KERNEL_STOP_SIGS) {
        if (sig != KERNEL_SIGSTOP && target->handlers[sig] == (void *)1 /* SIG_IGN */)
            return 0;
        if (sig != KERNEL_SIGSTOP && (target->signal_mask & (1U << sig))) {
            // Catchable stop signal is masked — just pend it
            target->pending_signals |= (1U << sig);
            return 0;
        }
        target->pending_signals &= ~(1U << KERNEL_SIGCONT); // discard pending CONT
        if (target->state == PROC_READY || target->state == PROC_BLOCKED) {
            target->stop_signal = (uint8_t)sig;
            proc_stop(target);
        } else
            // Target is RUNNING (unreachable single-core) — defer via pending bit
            target->pending_signals |= (1U << sig);
        return 0;
    }

    // Set the signal bit in the target's pending bitmask
    target->pending_signals |= (1U << sig);

    // Wake a blocked (sleeping) target so it re-enters the scheduler
    // and checks pending_signals before resuming user mode.
    // Use proc_unblock() directly rather than proc_wakeup() because
    // proc_wakeup() only matches processes sleeping on a specific channel
    // (e.g. it would miss a process blocked on terminal I/O with chan != target).
    //
    // Only wake if the signal is not currently masked.  Waking for a masked
    // signal causes a spurious return from proc_sleep: the post-wakeup fatal-
    // signal check correctly skips the masked signal, proc_sleep returns
    // normally, and control reaches sigsuspend's mask-restore line.  With the
    // old (empty) mask now active the still-pending signal is no longer blocked
    // and kills the process at the next trap return — wrong behaviour when the
    // caller expected sigsuspend to stay asleep for that signal.
    // SIGKILL is included in the wake condition as a safety net even though
    // sys_sigprocmask already prevents it from being set in signal_mask.
    if (target->state == PROC_BLOCKED &&
        (sig == KERNEL_SIGKILL || !(target->signal_mask & (1U << sig))))
        proc_unblock(target);

    return 0;
}

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (length == 0 || (offset & (PGSIZE - 1))) {
        return MAP_FAILED;
    }

    // Offset must be page-aligned (POSIX requirement).
    if (!(flags & MAP_ANONYMOUS) && (offset & (PGSIZE - 1))) {
        printk("sys_mmap: file offset not page-aligned\n");
        return MAP_FAILED;
    }

    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_mmap: no current process or page table\n");
        return MAP_FAILED;
    }

    // Acquire file reference for file-backed mappings.
    // f_get() bumps ref_count; the VMA holds it until munmap / proc_exit.
    // fd may be closed by the caller immediately after mmap() — this is legal (POSIX).
    file_t *mmap_file = NULL;
    if (!(flags & MAP_ANONYMOUS)) {
        mmap_file = f_get(p, fd);
        if (!mmap_file) {
            printk("sys_mmap: invalid fd %d\n", fd);
            return MAP_FAILED;
        }
        if (mmap_file->type != FILE_VNODE || !mmap_file->vnode) {
            f_put(mmap_file);
            printk("sys_mmap: fd %d is not a regular file\n", fd);
            return MAP_FAILED;
        }
    }

    size_t pages     = (length + PGSIZE - 1) / PGSIZE;
    size_t alloc_size = pages * PGSIZE;

    /* Enforce RLIMIT_AS: reject mappings that would push total virtual address
     * space usage over the soft limit.  Default is RLIM_INFINITY, so this is
     * a no-op unless the process calls setrlimit(RLIMIT_AS, ...).
     * POSIX: mmap() shall fail with ENOMEM if the limit would be exceeded. */
    if (p->rlimits[RLIMIT_AS].rlim_cur != RLIM_INFINITY) {
        uint64_t current_as = 0;
        for (struct vm_area *v = p->vma_list; v; v = v->next) {
            if (v->type & VMA_TYPE_DEVICE) continue; /* exclude kernel guard/MMIO reservations */
            current_as += v->end - v->start;
        }
        if (current_as + alloc_size > p->rlimits[RLIMIT_AS].rlim_cur) {
            if (mmap_file) f_put(mmap_file);
            /* POSIX: mmap() sets errno=ENOMEM when RLIMIT_AS is exceeded.
             * Return -ENOMEM so __syscall_ret encodes the correct errno;
             * userspace still receives MAP_FAILED (a0=-1). */
            return (void *)(intptr_t)(-ENOMEM);
        }
    }

    uintptr_t va;

    if ((flags & MAP_FIXED) && addr != NULL) {
        va = (uintptr_t)addr;
        if (va < USER_HEAP_START || va + alloc_size > USER_HEAP_END ||
            va + alloc_size < va) {
            if (mmap_file) f_put(mmap_file);
            printk("sys_mmap: fixed address out of range\n");
            return MAP_FAILED;
        }
        if (vma_find_overlap(p->vma_list, va, va + alloc_size)) {
            if (mmap_file) f_put(mmap_file);
            printk("sys_mmap: fixed address overlaps existing VMA\n");
            return MAP_FAILED;
        }
        for (uintptr_t page = va; page < va + alloc_size; page += PGSIZE) {
            uint64_t *pte = vmm_walk(p->pagetable, page, 0);
            if (pte && (*pte & PTE_V) && !(*pte & PTE_U)) {
                if (mmap_file) f_put(mmap_file);
                printk("sys_mmap: fixed address overlaps kernel mapping\n");
                return MAP_FAILED;
            }
        }
    } else {
        /* Find a free gap in the VMA list rather than bumping p->sz.
         * This lets munmap genuinely reclaim virtual address space: removing
         * a VMA creates a gap that vma_find_free_region will reuse on the
         * next mmap call.  p->sz is left to sbrk/brk exclusively. */
        va = vma_find_free_region(p->vma_list, alloc_size,
                                  USER_MMAP_START, USER_MMAP_START, USER_HEAP_END);
        if (va == 0) {
            if (mmap_file) f_put(mmap_file);
            printk("sys_mmap: out of virtual address space\n");
            return MAP_FAILED;
        }
    }

    uint32_t vma_prot = 0;
    if (prot & PROT_READ)  vma_prot |= VMA_PROT_READ;
    if (prot & PROT_WRITE) vma_prot |= VMA_PROT_WRITE;
    if (prot & PROT_EXEC)  vma_prot |= VMA_PROT_EXEC;

    uint32_t vma_flags = VMA_PRIVATE;
    if (flags & MAP_SHARED) vma_flags = VMA_SHARED;
    
    if ((flags & MAP_SHARED) && mmap_file) {
        printk("sys_mmap: MAP_SHARED file mapping — writes are NOT flushed to file\n");
    }

    uint32_t vma_type = (mmap_file) ? VMA_TYPE_FILE : VMA_TYPE_ANON;

    struct vm_area *new_vma = vma_create(va, va + alloc_size, vma_prot, vma_flags, vma_type);
    if (!new_vma) {
        if (mmap_file) f_put(mmap_file);
        return MAP_FAILED;
    }

    // Store file reference and offset in VMA.
    // The VMA owns the f_get ref; released in sys_munmap and vma_free_all.
    new_vma->file   = mmap_file;   // NULL for anonymous mappings
    new_vma->offset = (uint64_t)offset;

    if (vma_insert(&p->vma_list, new_vma) != 0) {
        if (mmap_file) f_put(mmap_file);
        vma_free(new_vma);
        return MAP_FAILED;
    }

    return (void *)va;
}

int sys_munmap(void *addr, size_t length) {
    if (addr == NULL || length == 0) {
        return -1;
    }

    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_munmap: no current process or page table\n");
        return -1;
    }

    uintptr_t unmap_start = (uintptr_t)addr;
    uintptr_t unmap_end   = unmap_start + PGROUNDUP(length);

    /* Flush dirty pages for MAP_SHARED file VMAs before unmapping.
     * POSIX mmap: modifications to a MAP_SHARED mapping must be propagated
     * to the underlying object no later than munmap(). PTE_D is set by
     * hardware on the first store to a page; read it before vmm_unmap clears it. */
    for (struct vm_area *sv = p->vma_list; sv; sv = sv->next) {
        if (sv->type != VMA_TYPE_FILE || !(sv->flags & VMA_SHARED) || !sv->file)
            continue;
        if (sv->start >= unmap_end || sv->end <= unmap_start)
            continue;
        uintptr_t wb_start = sv->start > unmap_start ? sv->start : unmap_start;
        uintptr_t wb_end   = sv->end   < unmap_end   ? sv->end   : unmap_end;
        vnode_t *vn = ((file_t *)sv->file)->vnode;
        if (!vn || !vn->fs_type) continue;
        for (uintptr_t va = wb_start; va < wb_end; va += PGSIZE) {
            uint64_t *pte = vmm_walk(p->pagetable, va, 0);
            if (pte && (*pte & PTE_V) && (*pte & PTE_U) && (*pte & PTE_D)) {
                uintptr_t pa = PTE_TO_PA(*pte);
                size_t off   = (size_t)(sv->offset + (va - sv->start));
                vn->fs_type->ops.write(vn, (void *)pa, off, PGSIZE);
            }
        }
    }

    /* Free any physical pages that were demand-faulted in. */
    for (uintptr_t page_va = unmap_start; page_va < unmap_end; page_va += PGSIZE) {
        uint64_t *pte = vmm_walk(p->pagetable, page_va, 0);
        if (pte && (*pte & PTE_V) && (*pte & PTE_U)) {
            uintptr_t pa = PTE_TO_PA(*pte);
            vmm_unmap(p->pagetable, page_va);
            pmm_free_page((void*)pa);
        }
    }

    /* Remove VMA(s) that cover this range (skip kernel device reservations). */
    struct vm_area *v = p->vma_list;
    while (v) {
        struct vm_area *next = v->next;
        if (v->start < unmap_end && v->end > unmap_start &&
                v->type != VMA_TYPE_DEVICE) {
            vma_remove(&p->vma_list, v);
            // Release the file reference held since sys_mmap (Step 1).
            // Safe to call with NULL (f_put is a no-op on NULL).
            if (v->type == VMA_TYPE_FILE && v->file)
                f_put((file_t *)v->file);
            vma_free(v);
        }
        v = next;
    }

    return 0;
}

intptr_t sys_brk(void *addr) {
    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_brk: no current process or page table\n");
        return (intptr_t)(-1L);
    }

    if (p->sz < USER_HEAP_START) {
        p->sz = USER_HEAP_START;
    }

    if (addr == NULL) {
        return (intptr_t)p->sz;
    }

    uintptr_t new_break = (uintptr_t)addr;

    if (new_break < USER_HEAP_START || new_break > USER_HEAP_END) {
        return (intptr_t)p->sz;   /* return unchanged break, not -1 */
    }

    uintptr_t old_break = p->sz;

    /* Find existing heap VMA or create one on first brk growth. */
    struct vm_area *heap_vma = NULL;
    for (struct vm_area *v = p->vma_list; v; v = v->next) {
        if (v->type == VMA_TYPE_HEAP) { heap_vma = v; break; }
    }

    if (new_break > old_break) {
        /* Enforce RLIMIT_DATA: heap size = new_break - USER_HEAP_START.
         * Default is RLIM_INFINITY; only active if process calls setrlimit.
         * POSIX: brk() sets errno=ENOMEM when RLIMIT_DATA is exceeded. */
        if (p->rlimits[RLIMIT_DATA].rlim_cur != RLIM_INFINITY) {
            uint64_t new_heap_size = new_break - USER_HEAP_START;
            if (new_heap_size > p->rlimits[RLIMIT_DATA].rlim_cur)
                return (intptr_t)old_break;   /* return old break on failure */
        }

        uintptr_t new_end = PGROUNDUP(new_break);

        if (heap_vma) {
            /* Extend existing heap VMA — no physical pages allocated. */
            if (new_end > heap_vma->end)
                heap_vma->end = new_end;
        } else {
            /* First brk growth: create the heap VMA covering all touched pages.
             * Round up to at least one page so vma_create's start < end holds. */
            uintptr_t vma_start = PGROUNDUP(old_break);
            uintptr_t vma_end   = (new_end > vma_start) ? new_end : vma_start + PGSIZE;
            heap_vma = vma_create(vma_start, vma_end,
                                  VMA_PROT_READ | VMA_PROT_WRITE,
                                  VMA_PRIVATE, VMA_TYPE_HEAP);
            if (!heap_vma) return (intptr_t)old_break;
            if (vma_insert(&p->vma_list, heap_vma) != 0) {
                vma_free(heap_vma);
                return (intptr_t)old_break;
            }
        }
    } else if (new_break < old_break) {
        /* Shrink: free any physical pages in the released range. */
        uintptr_t free_start = PGROUNDUP(new_break);
        uintptr_t free_end   = PGROUNDUP(old_break);
        for (uintptr_t va = free_start; va < free_end; va += PGSIZE) {
            uint64_t *pte = vmm_walk(p->pagetable, va, 0);
            if (pte && (*pte & PTE_V)) {
                uintptr_t pa = PTE_TO_PA(*pte);
                vmm_unmap(p->pagetable, va);
                pmm_free_page((void*)pa);
            }
        }
        if (heap_vma) {
            uintptr_t new_end = PGROUNDUP(new_break);
            if (new_end <= heap_vma->start) {
                vma_remove(&p->vma_list, heap_vma);
                vma_free(heap_vma);
            } else {
                heap_vma->end = new_end;
            }
        }
    }

    p->sz = new_break;
    return (intptr_t)new_break;   /* return new break on success */
}

void *sys_sbrk(intptr_t increment) {
    struct proc *p = current;
    if (p == NULL || p->pagetable == NULL) {
        printk("sys_sbrk: no current process or page table\n");
        return (void *) -1;
    }

    if (p->sz < USER_HEAP_START) {
        p->sz = USER_HEAP_START;
    }

    void* old_break = (void*)p->sz;

    if (increment == 0) {
        return old_break;
    }

    uintptr_t new_break = p->sz + increment;

    if (new_break < USER_HEAP_START || new_break > USER_HEAP_END) {
        return (void *) -1;
    }

    if ((uintptr_t)sys_brk((void*)new_break) != new_break) {
        /* brk returned old break (failure) rather than new_break (success).
         * Propagate ENOMEM so __syscall_ret sets errno correctly. */
        return (void *)(intptr_t)(-ENOMEM);
    }

    return old_break;
}

off_t sys_lseek(int fd, off_t offset, int whence) {
    return (off_t) file_lseek(fd, (size_t) offset, whence);
}

int sys_unlink(const char *path) {
    if (!current) return -1;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, path, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';
    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, kpath, MAX_PATH) < 0) {
        kfree(kpath); return -ENAMETOOLONG;
    }
    int ret = vfs_unlink(kpath);
    kfree(kpath);
    return ret;
}

int sys_link(const char *uoldpath, const char *unewpath) {
    if (!current) return -1;

    char *kold = kmalloc(MAX_PATH);
    char *knew = kmalloc(MAX_PATH);
    if (!kold || !knew) { kfree(kold); kfree(knew); return -ENOMEM; }

    if (strncpy_from_user(kold, uoldpath, MAX_PATH - 1) < 0) { kfree(kold); kfree(knew); return -EFAULT; }
    kold[MAX_PATH - 1] = '\0';
    if (strncpy_from_user(knew, unewpath, MAX_PATH - 1) < 0) { kfree(kold); kfree(knew); return -EFAULT; }
    knew[MAX_PATH - 1] = '\0';

    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kold, kold, MAX_PATH) < 0) {
        kfree(kold); kfree(knew); return -ENAMETOOLONG;
    }
    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              knew, knew, MAX_PATH) < 0) {
        kfree(kold); kfree(knew); return -ENAMETOOLONG;
    }

    int ret = vfs_link(kold, knew);
    kfree(kold); kfree(knew);
    return ret;
}

int sys_rename(const char *uoldpath, const char *unewpath) {
    if (!current) return -1;

    char *kold = kmalloc(MAX_PATH);
    char *knew = kmalloc(MAX_PATH);
    if (!kold || !knew) { kfree(kold); kfree(knew); return -ENOMEM; }

    if (strncpy_from_user(kold, uoldpath, MAX_PATH - 1) < 0) { kfree(kold); kfree(knew); return -EFAULT; }
    kold[MAX_PATH - 1] = '\0';
    if (strncpy_from_user(knew, unewpath, MAX_PATH - 1) < 0) { kfree(kold); kfree(knew); return -EFAULT; }
    knew[MAX_PATH - 1] = '\0';

    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kold, kold, MAX_PATH) < 0) {
        kfree(kold); kfree(knew); return -ENAMETOOLONG;
    }
    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              knew, knew, MAX_PATH) < 0) {
        kfree(kold); kfree(knew); return -ENAMETOOLONG;
    }

    int ret = vfs_rename(kold, knew);
    kfree(kold); kfree(knew);
    return ret;
}

int sys_symlink(const char *utarget, const char *ulinkpath) {
    if (!current) return -1;

    char *ktarget   = kmalloc(MAX_PATH);
    char *klinkpath = kmalloc(MAX_PATH);
    if (!ktarget || !klinkpath) { kfree(ktarget); kfree(klinkpath); return -ENOMEM; }

    if (strncpy_from_user(ktarget,   utarget,   MAX_PATH - 1) < 0) { kfree(ktarget); kfree(klinkpath); return -EFAULT; }
    ktarget[MAX_PATH - 1] = '\0';
    if (strncpy_from_user(klinkpath, ulinkpath, MAX_PATH - 1) < 0) { kfree(ktarget); kfree(klinkpath); return -EFAULT; }
    klinkpath[MAX_PATH - 1] = '\0';

    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              klinkpath, klinkpath, MAX_PATH) < 0) {
        kfree(ktarget); kfree(klinkpath); return -ENAMETOOLONG;
    }

    int ret = vfs_symlink(ktarget, klinkpath);
    kfree(ktarget); kfree(klinkpath);
    return ret;
}

ssize_t sys_readlink(const char *upath, char *ubuf, size_t bufsiz) {
    if (!current || bufsiz == 0) return -EINVAL;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;

    if (strncpy_from_user(kpath, upath, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, kpath, MAX_PATH) < 0) {
        kfree(kpath); return -ENAMETOOLONG;
    }

    char *kbuf = kmalloc(MAX_PATH);
    if (!kbuf) { kfree(kpath); return -ENOMEM; }
    size_t copy_size = bufsiz < MAX_PATH ? bufsiz : MAX_PATH;
    ssize_t r = vfs_readlink(kpath, kbuf, copy_size);
    kfree(kpath);
    if (r < 0) { kfree(kbuf); return r; }

    int rc = copy_to_user(ubuf, kbuf, (size_t)r);
    kfree(kbuf);
    return (rc < 0) ? -EFAULT : r;
}

int sys_mkdir(const char *path, mode_t mode) {
    if (!current) return -1;

    char *_pathbufs = kmalloc(2 * MAX_PATH);
    if (!_pathbufs) return -ENOMEM;
    char *kpath    = _pathbufs;
    char *resolved = _pathbufs + MAX_PATH;
    if (strncpy_from_user(kpath, path, MAX_PATH - 1) < 0) { kfree(_pathbufs); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    /* vfs_mkdir uses vfs_find_mount() prefix matching, so it needs a
     * normalized absolute path.  dentry_normalize_path handles ".", "..",
     * double slashes, and relative-to-cwd expansion. */
    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, resolved, MAX_PATH) < 0) { kfree(_pathbufs); return -ENAMETOOLONG; }

    int ret = vfs_mkdir(resolved, mode);
    kfree(_pathbufs);
    return ret;
}

int sys_sync(void) {
    return block_cache_flush();
}

int sys_fsync(int fd) {
    if (!current)
        return -EINVAL;
    file_t *f = fd_get(&current->fds, fd);
    if (!f)
        return -EBADF;
    if (f->type != FILE_VNODE)
        return -EINVAL;    /* pipes and stdio have no storage device */
    return block_cache_flush();
}

int sys_fdatasync(int fd) {
    return sys_fsync(fd);  /* data and metadata share the same block-cache path */
}

int sys_sigprocmask(int how, const uint32_t *set, uint32_t *oldset) {
    if (!current) return -1;

    if (oldset && copy_to_user(oldset, &current->signal_mask, sizeof(uint32_t)) < 0)
        return -EFAULT;

    if (!set) return 0;

    uint32_t val;
    if (copy_from_user(&val, set, sizeof(uint32_t)) < 0) return -EFAULT;

    uint32_t new_mask = current->signal_mask;

    switch (how) {
        case 0: new_mask |=  val; break;   // SIG_BLOCK   = 0
        case 1: new_mask &= ~val; break;   // SIG_UNBLOCK = 1
        case 2: new_mask  =  val; break;   // SIG_SETMASK = 2
        default: return -1;
    }

    // SIGKILL (9), SIGSTOP (19), SIGILL (4), SIGABRT (6), SIGTRAP (5), SIGBUS (7)
    // can never be masked.
    // SIGABRT: abort() must terminate; a masked SIGABRT would leave the process alive.
    // SIGTRAP: EBREAK fires repeatedly if SIGTRAP is masked (infinite re-fault loop).
    // SIGBUS: misaligned access fires repeatedly if SIGBUS is masked (same loop).
    // SIGHUP, SIGUSR1, SIGUSR2, SIGSYS are software-only; masking them is legal.
    new_mask &= ~((1U << KERNEL_SIGKILL)  | (1U << KERNEL_SIGSTOP)  |
                (1U << KERNEL_SIGILL)   | (1U << KERNEL_SIGABRT)  |
                (1U << KERNEL_SIGTRAP)  | (1U << KERNEL_SIGBUS));
    current->signal_mask = new_mask;
    return 0;
}

int sys_chdir(const char *path) {
    if (!current || !path) return -EINVAL;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, path, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';
    int ret = vfs_chdir(kpath);
    kfree(kpath);
    return ret;
}

char *sys_getcwd(char *buf, size_t size) {
    if (!current || !buf || size == 0 || !current->fs) return NULL;

    // Build the absolute path from the cwd dentry chain
    char *kbuf = kmalloc(MAX_PATH);
    if (!kbuf) return NULL;
    if (fs_struct_get_cwd(current->fs, kbuf, MAX_PATH) < 0) { kfree(kbuf); return NULL; }

    size_t cwd_len = strlen(kbuf);
    if (cwd_len + 1 > size) { kfree(kbuf); return NULL; }   // user buffer too small

    copy_to_user(buf, kbuf, cwd_len + 1);
    kfree(kbuf);
    return buf;
}

// Fills user buffer `dirp` with fs_dent records for the directory
// referenced by `fd`.  Returns bytes written, 0 at end-of-directory, -errno.
int sys_fs_getdents(int fd, void *dirp, uint32_t count)
{
    if (!current || !dirp) return -EINVAL;

    file_t *f = f_get(current, fd);
    if (!f || f->type != FILE_VNODE) {
        if (f)
            f_put(f);
        return -EBADF;
    }
    if (!f->dentry || !f->vnode || f->vnode->type != VNODE_DIR) {
        f_put(f);
        return -ENOTDIR;
    }

    if (count == 0) {
        f_put(f);
        return 0;
    }

    char *kbuf = kmalloc(count);
    if (!kbuf) {
        f_put(f);
        return -ENOMEM;
    }

    uint32_t entries_read = 0;
    int n = vfs_readdir(f->dentry, (uint32_t)f->offset,
                        kbuf, count, &entries_read);

    if (n > 0) {
        f->offset += entries_read;
        copy_to_user(dirp, kbuf, (size_t) n);
    }

    kfree(kbuf);
    f_put(f);
    return n;
}
/* ------------------------------------------------------------------ */
/* sys_getrusage                                                        */
/* ------------------------------------------------------------------ */

int sys_getrusage(int who, struct rusage *usage) {
    if (!current || !usage) return -1;

    uint64_t ticks;
    if (who == RUSAGE_SELF) {
        ticks = current->utime_ticks;
    } else if (who == RUSAGE_CHILDREN) {
        ticks = current->children_utime;
    } else {
        return -1;
    }

    struct rusage ku;
    ku.ru_utime.tv_sec  = ticks / TICKS_PER_SEC;
    ku.ru_utime.tv_usec = (ticks % TICKS_PER_SEC) * (1000000ULL / TICKS_PER_SEC);
    ku.ru_stime.tv_sec  = 0;
    ku.ru_stime.tv_usec = 0;

    if (copy_to_user(usage, &ku, sizeof(ku)) < 0) return -EFAULT;
    return 0;
}

/* ------------------------------------------------------------------ */
/* sys_gettimeofday                                                     */
/* ------------------------------------------------------------------ */

int sys_gettimeofday(struct timeval_k *tv, void *tz) {
    if (tv) {
        uint64_t ns = rtc_read_ns();
        struct timeval_k ktv;
        ktv.tv_sec  = ns / 1000000000ULL;
        ktv.tv_usec = (ns % 1000000000ULL) / 1000ULL;
        copy_to_user(tv, &ktv, sizeof(ktv));
    }
    if (tz) {
        int zeros[2] = {0, 0};
        copy_to_user(tz, zeros, sizeof(zeros));
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* sys_nice                                                             */
/* ------------------------------------------------------------------ */

/*
 * sys_nice(increment) — adjust the calling process's scheduling priority.
 *
 * Convention (matching struct proc.priority): 0 = highest, 19 = lowest,
 * which mirrors the Unix nice-value range [0, 19] used by this kernel.
 *
 * Positive increment lowers priority (background work).
 * Negative increment raises priority; clamped to 0 regardless of privilege
 * because this kernel has no uid concept.
 *
 * POSIX semantics:
 *   nice(0)  — returns current priority without changing it.
 *   Return value is the new priority on success, -1 if current == NULL.
 *
 * Overflow safety: intermediate arithmetic is widened to long (64-bit on
 * RISC-V) before clamping so that INT_MIN/INT_MAX arguments cannot wrap.
 */
int sys_nice(int increment) {
    if (!current) return -1;
    long new_prio = (long)current->priority + (long)increment;
    if (new_prio < 0)  new_prio = 0;
    if (new_prio > 19) new_prio = 19;
    int old_prio = current->priority;
    current->priority = (int)new_prio;
    /* Lowered scheduling priority — yield if a better process is waiting. */
    if ((int)new_prio > old_prio)
        proc_preempt_if_needed();
    return (int)new_prio;
}

/*
 * sys_yield() — voluntarily relinquish the CPU.
 *
 * Calls proc_yield(), which re-enqueues the calling process as READY at
 * its current MLFQ level (priority preserved, no demotion) and switches
 * to the scheduler.  The process resumes normally once the scheduler
 * selects it again.
 *
 * If no other process is runnable, proc_yield() re-selects the same
 * process immediately — one extra context switch but no functional issue.
 *
 * POSIX sched_yield() always returns 0.
 */
int sys_yield(void) {
    proc_yield();
    return 0;
}

/*
 * sys_pause() — suspend the calling process until any signal is delivered.
 *
 * Uses &current->pending_signals as the sleep channel so the timer's
 * proc_wakeup(p) call (which uses the proc pointer as channel) cannot
 * accidentally wake us. Only proc_unblock() from sys_kill wakes us.
 *
 * proc_sleep() checks SIGSTOP and KERNEL_FATAL_SIGS on wakeup and
 * handles them (stop or exit) before returning here.
 *
 * POSIX: always returns -1 (EINTR).
 */
int sys_pause(void) {
    // If a non-fatal, unmasked signal is already pending, return immediately.
    // (Fatal ones were caught at syscall entry; we'd never reach here if they were set.)
    uint32_t check = current->pending_signals & ~current->signal_mask
                     & ~KERNEL_FATAL_SIGS & ~KERNEL_STOP_SIGS;
    if (check) return -1;
    proc_sleep(&current->pending_signals);
    return -1;
}

/*
 * A POSIX-conforming ps should show at minimum: PID, PPID, state, priority (if implemented), and name
 * NOTE: As of now, PIDs are strictly incremented only. If we look to implement PID recycling, this would risk PID races,
 * and after a certain PID value limit reached, would look for the lowest available PID value.
*/
int sys_procinfo(struct proc_info *infos, int max_count) {
    int count = 0;
    for (int i = 0; i < MAX_PROCS && count < max_count; i++) {
        struct proc *p = &proctab[i];
        if (p->state == PROC_UNUSED) continue;
        struct proc_info ki;
        ki.pid       = p->pid;
        ki.ppid      = p->parent ? p->parent->pid : 0;
        strncpy(ki.name, p->name, 15);
        ki.name[15]  = '\0';
        ki.state     = (int)p->state;
        ki.priority  = p->priority;
        ki.cpu_ticks = p->utime_ticks;
        if (copy_to_user(infos + count, &ki, sizeof(ki)) < 0) return -EFAULT;
        count++;
    }
    return count;
}


// trap/trapsyscall.c

int sys_stat(const char *upath, struct stat *ustat)
{
    if (!current || !upath || !ustat) return -EINVAL;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, upath, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    struct stat kst;
    int ret = file_stat(kpath, &kst);
    kfree(kpath);
    if (ret != 0) return ret;

    if (copy_to_user(ustat, &kst, sizeof(struct stat)) < 0) return -EFAULT;
    return 0;
}

int sys_lstat(const char *upath, struct stat *ustat)
{
    if (!current || !upath || !ustat) return -EINVAL;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, upath, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    struct stat kst;
    int ret = file_lstat(kpath, &kst);
    kfree(kpath);
    if (ret != 0) return ret;

    if (copy_to_user(ustat, &kst, sizeof(struct stat)) < 0) return -EFAULT;
    return 0;
}

int sys_access(const char *upath, int mode)
{
    if (!current || !upath) return -EINVAL;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, upath, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    struct stat kst;
    int ret = file_stat(kpath, &kst);
    kfree(kpath);
    if (ret != 0) return ret;

    if (mode == 0 /* F_OK */) return 0;

    /* POSIX: check requested permission bits against inode mode.
     * No uid/gid — treat caller as root owning everything, so check
     * the union of owner|group|other bits for each requested access. */
    if ((mode & 4 /* R_OK */) && !(kst.st_mode & (0400 | 0040 | 0004)))
        return -EACCES;
    if ((mode & 2 /* W_OK */) && !(kst.st_mode & (0200 | 0020 | 0002)))
        return -EACCES;
    if ((mode & 1 /* X_OK */) && !(kst.st_mode & (0100 | 0010 | 0001)))
        return -EACCES;

    return 0;
}

int sys_fstat(int fd, struct stat *ustat)
{
    if (!current || !ustat) return -EINVAL;

    struct stat kst;
    int ret = file_fstat(fd, &kst);
    if (ret != 0) return ret;

    if (copy_to_user(ustat, &kst, sizeof(struct stat)) < 0) return -EFAULT;
    return 0;
}

int sys_fstatat(int dirfd, const char *upath, struct stat *ustat, int flags)
{
    if (!current || !upath || !ustat) return -EINVAL;

    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, upath, MAX_PATH - 1) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    int no_follow = (flags & AT_SYMLINK_NOFOLLOW) != 0;

    // Absolute path or AT_FDCWD: resolve from CWD/root as stat/lstat would
    if (kpath[0] == '/' || dirfd == AT_FDCWD) {
        struct stat kst;
        int ret = no_follow ? file_lstat(kpath, &kst) : file_stat(kpath, &kst);
        kfree(kpath);
        if (ret != 0) return ret;
        if (copy_to_user(ustat, &kst, sizeof(struct stat)) < 0) return -EFAULT;
        return 0;
    }

    // Relative path with real dirfd: build full path from that directory's dentry
    file_t *f = f_get(current, dirfd);
    if (!f) { kfree(kpath); return -EBADF; }
    if (!f->dentry) { f_put(f); kfree(kpath); return -ENOTDIR; }

    char *fullpath = kmalloc(MAX_PATH);
    if (!fullpath) { f_put(f); kfree(kpath); return -ENOMEM; }

    if (dentry_build_path(f->dentry, vfs_get_root(), fullpath, MAX_PATH) != 0) {
        f_put(f); kfree(kpath); kfree(fullpath);
        return -ENAMETOOLONG;
    }
    f_put(f);

    size_t dlen = strlen(fullpath);
    size_t plen = strlen(kpath);
    if (dlen + 1 + plen >= MAX_PATH) { kfree(kpath); kfree(fullpath); return -ENAMETOOLONG; }
    if (dlen == 0 || fullpath[dlen - 1] != '/') { fullpath[dlen] = '/'; dlen++; }
    strncpy(fullpath + dlen, kpath, MAX_PATH - dlen - 1);
    fullpath[MAX_PATH - 1] = '\0';
    kfree(kpath);

    struct stat kst;
    int ret = no_follow ? file_lstat(fullpath, &kst) : file_stat(fullpath, &kst);
    kfree(fullpath);
    if (ret != 0) return ret;
    if (copy_to_user(ustat, &kst, sizeof(struct stat)) < 0) return -EFAULT;
    return 0;
}

int sys_chmod(const char *upath, mode_t mode) {
    if (!current || !upath) return -EINVAL;

    char *_pathbufs = kmalloc(2 * MAX_PATH);
    if (!_pathbufs) return -ENOMEM;
    char *kpath    = _pathbufs;
    char *resolved = _pathbufs + MAX_PATH;
    if (strncpy_from_user(kpath, upath, MAX_PATH - 1) < 0) { kfree(_pathbufs); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, resolved, MAX_PATH) < 0) { kfree(_pathbufs); return -ENAMETOOLONG; }

    int ret = vfs_chmod(resolved, mode);
    kfree(_pathbufs);
    return ret;
}

int sys_fchmod(int fd, mode_t mode) {
    if (!current) return -EINVAL;

    file_t *f = fd_get(&current->fds, fd);
    if (!f) return -EBADF;
    if (f->type != FILE_VNODE || !f->vnode) return -EBADF;

    return vfs_fchmod(f->vnode, mode);
}

int sys_rmdir(const char *path) {
    if (!current || !path) return -EINVAL;

    char *_pathbufs = kmalloc(2 * MAX_PATH);
    if (!_pathbufs) return -ENOMEM;
    char *kpath    = _pathbufs;
    char *resolved = _pathbufs + MAX_PATH;
    if (strncpy_from_user(kpath, path, MAX_PATH - 1) < 0) { kfree(_pathbufs); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    if (dentry_normalize_path(current->fs->cwd, vfs_get_root(),
                              kpath, resolved, MAX_PATH) < 0) { kfree(_pathbufs); return -ENAMETOOLONG; }

    int ret = vfs_rmdir(resolved);
    kfree(_pathbufs);
    return ret;
}

unsigned int sys_alarm(unsigned int seconds) {
    uint64_t old = current->alarm_tick;

    if (seconds == 0) {
        // alarm(0) cancels any pending alarm
        current->alarm_tick = 0;
    } else {
        current->alarm_tick = timer_ticks + (uint64_t)seconds * TICKS_PER_SEC;
    }

    // Return seconds remaining from previous alarm, or 0 if none / already fired
    if (old == 0 || old <= timer_ticks)
        return 0;
    // Ceiling division: round up so partial ticks count as a full second
    return (unsigned int)((old - timer_ticks + TICKS_PER_SEC - 1) / TICKS_PER_SEC);
}

int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    if (!current) return -1;
    if (sig < 1 || sig >= 32) return -EINVAL;
    // SIGKILL and SIGSTOP cannot be caught or ignored — POSIX
    if (sig == SIGKILL || sig == SIGSTOP) return -EINVAL;

    if (oldact != NULL) {
        struct sigaction kold = { .sa_handler = (sighandler_t)current->handlers[sig],
                                  .sa_mask    = current->sa_masks[sig],
                                  .sa_flags   = (int)current->sig_flags[sig] };
        if (copy_to_user(oldact, &kold, sizeof(kold)) < 0) return -EFAULT;
    }

    if (act != NULL) {
        struct sigaction kact;
        if (copy_from_user(&kact, act, sizeof(kact)) < 0) return -EFAULT;
        /* Custom handler: require SA_RESTORER so the kernel knows the return
         * trampoline address.  The libc sigaction() wrapper always injects it;
         * raw calls without it (e.g. kernel unit tests) are rejected. */
        if (kact.sa_handler != SIG_DFL && kact.sa_handler != SIG_IGN &&
            !(kact.sa_flags & SA_RESTORER))
            return -EINVAL;
        current->handlers[sig]    = (void *)kact.sa_handler;
        current->sa_masks[sig]    = kact.sa_mask & ~((1U << KERNEL_SIGKILL)  | (1U << KERNEL_SIGSTOP)  |
                                                      (1U << KERNEL_SIGILL)   | (1U << KERNEL_SIGABRT)  |
                                                      (1U << KERNEL_SIGTRAP)  | (1U << KERNEL_SIGBUS)); /* P1 field */
        current->sa_restorer[sig] = (kact.sa_flags & SA_RESTORER)       /* P1 field */
                                    ? (void *)kact.sa_restorer : NULL;
        current->sig_flags[sig] = (uint32_t)kact.sa_flags & SA_KNOWN_FLAGS;
    }

    return 0;
}

int sys_sigpending(uint32_t *set) {
    if (!current || !set) return -EINVAL;

    uint32_t pending = current->pending_signals & current->signal_mask;
    if (copy_to_user(set, &pending, sizeof(uint32_t)) < 0) return -EFAULT;
    return 0;
}

/*
 * sys_sigsuspend() — atomically replace signal mask and suspend until a
 * signal is delivered.
 *
 * POSIX: atomically installs *mask as the process signal mask, then blocks
 * until any signal whose action would terminate the process or invoke a
 * signal-catching function is delivered.  The original mask is always
 * restored before returning, and the call always returns -1 (EINTR).
 *
 * Implementation notes:
 *   - The SUM bit is only needed to read *mask (a userspace pointer).
 *     Writing current->signal_mask is a kernel-struct write; no SUM needed.
 *   - After installing the new mask a signal that was previously blocked
 *     may now be pending.  If we called proc_sleep() unconditionally nobody
 *     would ever call proc_wakeup() for that already-set bit, causing the
 *     process to sleep forever.  We therefore run the same signal-delivery
 *     checks that proc_sleep() performs on wakeup *before* sleeping, so that
 *     pre-pending signals are handled identically to signals that arrive
 *     while we are blocked.
 */
int sys_sigsuspend(const uint32_t *mask) {
    if (!current || !mask) return -EINVAL;

    uint32_t new_mask;
    if (copy_from_user(&new_mask, mask, sizeof(uint32_t)) < 0) return -EINVAL;

    /* Atomically install new mask; save old mask for restoration on return. */
    uint32_t old_mask = current->signal_mask;
    current->signal_mask = new_mask;

    /*
     * Pre-sleep signal sweep — mirrors proc_sleep()'s post-wakeup checks.
     * This handles signals that were pending-but-blocked under old_mask and
     * are now unmasked by new_mask, which proc_wakeup() would never fire for.
     */

    /* Step 1: Stop signals — SIGSTOP unmaskable; others respect signal_mask. */
    uint32_t pending_stop = current->pending_signals & KERNEL_STOP_SIGS;
    {
        uint32_t catchable_stops = pending_stop & ~(1U << KERNEL_SIGSTOP);
        pending_stop &= ~(catchable_stops & current->signal_mask);
    }
    if (pending_stop) {
        current->pending_signals &= ~pending_stop;
        current->stop_signal = (uint8_t)kernel_ctz(pending_stop);
        proc_stop(current);
        /* Resumes here after SIGCONT is delivered. */
    }

    /* Step 2: Clear SIG_IGN-disposed and default-ignore pending signals. */
    {
        uint32_t tmp = current->pending_signals & ~current->signal_mask;
        while (tmp) {
            int s = kernel_ctz((uint32_t)tmp);
            tmp &= tmp - 1;
            if (s == KERNEL_SIGKILL || s == KERNEL_SIGSTOP)
                continue;
            if (current->handlers[s] == (void *)SIG_IGN ||
                ((1U << s) & KERNEL_DFL_IGNORE_SIGS &&
                 current->handlers[s] == (void *)SIG_DFL))
                current->pending_signals &= ~(1u << s);
        }
    }

    /* Step 3: Fatal signals already pending under the new mask — terminate. */
    uint32_t unmasked = current->pending_signals & ~current->signal_mask;
    if (unmasked & KERNEL_FATAL_SIGS) {
        current->pending_signals = 0;
        proc_exit(-kernel_ctz((uint32_t)(unmasked & KERNEL_FATAL_SIGS)));
        /* unreachable */
    }

    /* Step 4: Non-fatal pending signals → return EINTR immediately.
     * Mirrors sys_pause()'s early-return pattern.  The signal bits remain in
     * pending_signals and are visible to the caller via sigpending().
     * The original mask is restored first so the signals are correctly masked
     * or unmasked for subsequent operations. */
    uint32_t nonfatal = current->pending_signals & ~current->signal_mask
                        & ~KERNEL_FATAL_SIGS & ~KERNEL_STOP_SIGS;
    if (!nonfatal) {
        /* No immediately actionable signals: block until one is delivered. */
        proc_sleep(&current->pending_signals);
        /* proc_sleep() handles SIGSTOP, SIG_IGN, and fatal signals on wakeup. */
    }

    /* Restore original mask — POSIX requires this before sigsuspend returns. */
    current->signal_mask = old_mask;
    return -1; /* POSIX: always returns -1 (errno = EINTR) */
}

pid_t sys_getpgrp(void) {
    if (!current) return -1;
    return current->pgid;
}

/*
 * sys_setpgid — move process pid into process group pgid.
 *
 * POSIX constraints enforced:
 *   - Caller may only act on itself (pid == 0 or pid == current->pid) or a
 *     direct child that has not yet called exec (we can't track the exec bit
 *     cheaply, so we allow it on direct children unconditionally).
 *   - A session leader cannot change its process group.
 *   - The target pgid must already exist within the same session, OR pgid == 0
 *     (which means "create a new group with target->pid as the pgid").
 */
int sys_setpgid(pid_t pid, pid_t pgid) {
    if (!current) return -1;

    struct proc *target;
    if (pid == 0 || pid == current->pid) {
        target = current;
    } else {
        target = proc_find(pid);
        if (!target || target->state == PROC_UNUSED || target->state == PROC_ZOMBIE)
            return -ESRCH;
        /* Caller must be the direct parent */
        if (target->parent != current)
            return -ESRCH;
    }

    /* POSIX: cannot change the process group of a session leader */
    if (target->sid == target->pid)
        return -EPERM;

    /* pgid == 0 means "join a new group with the target's own pid" */
    pid_t new_pgid = (pgid == 0) ? target->pid : pgid;

    /* If joining an existing group, it must exist within the same session.
     * Be tolerant of the common pipeline race where one child tries to join
     * another child's soon-to-be-created group before that peer has finished
     * setpgid(0, 0).  In that case, allow the join as long as the target pgid
     * names a live process in the same session. */
    if (new_pgid != target->pid) {
        int found = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proctab[i];
            if (p->state != PROC_UNUSED && p->state != PROC_ZOMBIE &&
                p->sid == target->sid &&
                (p->pgid == new_pgid || p->pid == new_pgid)) {
                found = 1;
                break;
            }
        }
        if (!found) return -EPERM;
    }

    target->pgid = new_pgid;
    return 0;
}

/*
 * sys_setsid — create a new session.
 *
 * The calling process must not already be a process group leader.
 * Returns the new session id (== current->pid) on success, or -EPERM.
 */
pid_t sys_setsid(void) {
    if (!current) return -1;
    /* POSIX: process group leader cannot call setsid */
    if (current->pgid == current->pid)
        return -EPERM;
    current->sid              = current->pid;
    current->pgid             = current->pid;
    current->is_session_leader = 1;
    /* POSIX: setsid() creates a new session with NO controlling terminal.
     * Do NOT set console_term.session_leader here — that would make every
     * short-lived child that calls setsid() (e.g. test helpers) appear to
     * own the console, causing proc_exit() to deliver spurious SIGHUP to
     * the foreground process group when the child exits. */
    return current->sid;
}

/*
 * sys_getpgid — return the process group id of process pid.
 * pid == 0 means the calling process.
 */
pid_t sys_getpgid(pid_t pid) {
    if (!current) return -1;
    if (pid == 0 || pid == current->pid)
        return current->pgid;
    struct proc *p = proc_find(pid);
    if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
        return -1;
    return p->pgid;
}

/*
 * sys_getsid — return the session id of process pid.
 * pid == 0 means the calling process.
 * POSIX: -ESRCH if no such process. SBUnix has a single implicit session
 * domain (no security partitioning), so the XSI EPERM clause does not apply.
 */
pid_t sys_getsid(pid_t pid) {
    if (!current) return -1;
    if (pid == 0 || pid == current->pid)
        return current->sid;
    struct proc *p = proc_find(pid);
    if (!p || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE)
        return -ESRCH;
    return p->sid;
}

/*
 * sys_tcsetpgrp — set the foreground process group of the terminal.
 *
 * Simplified version: no fd parameter, always operates on console_term.
 * The shell calls this before running each foreground job so the terminal
 * knows which process group to signal on Ctrl-Z / Ctrl-C.
 */
int sys_tcsetpgrp(pid_t pgid) {
    if (!current) return -1;
    console_term.foreground_pgid = pgid;
    return 0;
}

/*
 * sys_tcgetpgrp — return the foreground process group of the terminal.
 *
 * When no process has yet claimed the console (foreground_pgid == 0), return
 * the caller's own pgid.  This lets the first interactive shell bootstrap job
 * control: its tcgetpgrp(fd) == getpgrp() check passes, and it then calls
 * tcsetpgrp() to properly register itself as the foreground group.
 */
pid_t sys_tcgetpgrp(void) {
    if (console_term.foreground_pgid == 0 && current)
        return current->pgid;
    return console_term.foreground_pgid;
}

int sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;

    /* Read request from userspace via safe copy (sets/clears SUM). */
    struct timespec kreq;
    copy_from_user(&kreq, req, sizeof(kreq));
    long sec  = kreq.tv_sec;
    long nsec = kreq.tv_nsec;

    if (sec < 0 || nsec < 0 || nsec >= 1000000000L) return -EINVAL;

    /* Convert to ticks, rounding up to the next tick boundary. */
    uint64_t ns_per_tick = 1000000000ULL / TICKS_PER_SEC;
    uint64_t ticks = (uint64_t)sec * TICKS_PER_SEC +
                     ((uint64_t)nsec + ns_per_tick - 1) / ns_per_tick;
    if (ticks == 0) {
        if (rem) {
            struct timespec krem = {0, 0};
            copy_to_user(rem, &krem, sizeof(krem));
        }
        return 0;
    }
    /*
     * +1 tick: the process is at an unknown position within the current
     * 100 ms tick interval when sys_nanosleep is called.  Without this,
     * a 1-second request could wake after as little as ~900 ms of real
     * time (if called 99 ms into the current tick), violating POSIX
     * "at least the requested duration" semantics.  The extra tick makes
     * the worst-case sleep (1001 ms for a 1 s request) rather than
     * risking under-sleep.
     */
    ticks += 1;

    /*
     * Sleep on the process itself.  timer_handler() detects expiry via
     * wake_tick and calls proc_wakeup(p) where p == current, which matches
     * the channel we pass to proc_sleep().  Signal delivery uses
     * proc_unblock() and wakes us regardless of channel.
     */
    current->wake_tick = timer_ticks + ticks;
    proc_sleep(current);

    /*
     * Determine why we woke.  timer_handler() clears wake_tick to 0 before
     * calling proc_wakeup(), so wake_tick == 0 means normal expiry and
     * wake_tick != 0 means a signal interrupted us early.
     */
    if (current->wake_tick == 0) {
        if (rem) {
            struct timespec krem = {0, 0};
            copy_to_user(rem, &krem, sizeof(krem));
        }
        return 0;
    }

    /* Signal interrupted — report remaining time, clear the pending tick. */
    uint64_t ticks_left = current->wake_tick - timer_ticks;
    current->wake_tick = 0;
    if (rem) {
        struct timespec krem;
        krem.tv_sec  = (long)(ticks_left / TICKS_PER_SEC);
        krem.tv_nsec = (long)((ticks_left % TICKS_PER_SEC) * ns_per_tick);
        copy_to_user(rem, &krem, sizeof(krem));
    }
    return -EINTR;
}

int sys_clock_nanosleep(clockid_t clockid, int flags,
                        const struct timespec *req, struct timespec *rem) {
    if (!req) return -1;
    if (clockid == CLOCK_REALTIME) return -1;
    if (clockid != CLOCK_MONOTONIC) return -EINVAL;

    /* Read request from userspace via safe copy (sets/clears SUM). */
    struct timespec kreq;
    copy_from_user(&kreq, req, sizeof(kreq));
    long sec  = kreq.tv_sec;
    long nsec = kreq.tv_nsec;

    if (nsec < 0 || nsec >= 1000000000L) return -EINVAL;

    uint64_t ns_per_tick = 1000000000ULL / TICKS_PER_SEC;
    uint64_t ticks;

    if (flags == TIMER_ABSTIME) {
        /* Absolute time: sleep until the given tick value is reached. */
        if (sec < 0) return 0;   /* already in the past */
        uint64_t abs_ticks = (uint64_t)sec * TICKS_PER_SEC +
                             ((uint64_t)nsec + ns_per_tick - 1) / ns_per_tick;
        if (abs_ticks <= timer_ticks) return 0;
        ticks = abs_ticks - timer_ticks;
        rem = NULL;   /* POSIX: rem unused for TIMER_ABSTIME */
    } else {
        /* Relative time — same logic as sys_nanosleep(). */
        if (sec < 0) return -EINVAL;
        ticks = (uint64_t)sec * TICKS_PER_SEC +
                ((uint64_t)nsec + ns_per_tick - 1) / ns_per_tick;
        if (ticks > 0) ticks += 1;  /* same partial-tick fix as sys_nanosleep */
    }

    if (ticks == 0) return 0;

    current->wake_tick = timer_ticks + ticks;
    proc_sleep(current);

    if (current->wake_tick == 0) {
        if (rem) {
            struct timespec krem = {0, 0};
            copy_to_user(rem, &krem, sizeof(krem));
        }
        return 0;
    }

    uint64_t ticks_left = current->wake_tick - timer_ticks;
    current->wake_tick = 0;
    if (rem) {
        struct timespec krem;
        krem.tv_sec  = (long)(ticks_left / TICKS_PER_SEC);
        krem.tv_nsec = (long)((ticks_left % TICKS_PER_SEC) * ns_per_tick);
        copy_to_user(rem, &krem, sizeof(krem));
    }
    return -EINTR;
}

/* ---------- helpers: convert between ticks and timeval_k ---------- */

static void ticks_to_timeval(uint64_t ticks, struct timeval_k *tv) {
    tv->tv_sec  = ticks / TICKS_PER_SEC;
    tv->tv_usec = (ticks % TICKS_PER_SEC) * (1000000ULL / TICKS_PER_SEC);
}

static uint64_t timeval_to_ticks(const struct timeval_k *tv) {
    uint64_t us_per_tick = 1000000ULL / TICKS_PER_SEC;
    return tv->tv_sec * TICKS_PER_SEC +
           (tv->tv_usec + us_per_tick - 1) / us_per_tick;   /* round up */
}

/* ---------- sys_getitimer / sys_setitimer ---------- */

int sys_getitimer(int which, struct itimerval_k *curr_value) {
    if (which != ITIMER_REAL) return -EINVAL;
    if (!curr_value) return -EINVAL;

    struct itimerval_k kval;
    memset(&kval, 0, sizeof(kval));

    if (current->itimer_value != 0 && current->itimer_value > timer_ticks) {
        ticks_to_timeval(current->itimer_value - timer_ticks, &kval.it_value);
    }
    ticks_to_timeval(current->itimer_interval, &kval.it_interval);

    copy_to_user(curr_value, &kval, sizeof(kval));
    return 0;
}

int sys_setitimer(int which, const struct itimerval_k *new_value,
                  struct itimerval_k *old_value) {
    if (which != ITIMER_REAL) return -EINVAL;

    /* Return previous timer state if requested */
    if (old_value) {
        struct itimerval_k kold;
        memset(&kold, 0, sizeof(kold));
        if (current->itimer_value != 0 && current->itimer_value > timer_ticks)
            ticks_to_timeval(current->itimer_value - timer_ticks, &kold.it_value);
        ticks_to_timeval(current->itimer_interval, &kold.it_interval);
        copy_to_user(old_value, &kold, sizeof(kold));
    }

    /* Arm or disarm the timer */
    if (new_value) {
        struct itimerval_k knew;
        copy_from_user(&knew, new_value, sizeof(knew));

        uint64_t val_ticks = timeval_to_ticks(&knew.it_value);
        uint64_t int_ticks = timeval_to_ticks(&knew.it_interval);

        if (val_ticks == 0) {
            /* Disarm */
            current->itimer_value    = 0;
            current->itimer_interval = 0;
        } else {
            current->itimer_value    = timer_ticks + val_ticks;
            current->itimer_interval = int_ticks;
        }
    }

    return 0;
}

long sys_sysconf(int name) {
    switch (name) {
    case SBUNIX_SC_OPEN_MAX:          return SBUNIX_OPEN_MAX;
    case SBUNIX_SC_CHILD_MAX:         return SBUNIX_CHILD_MAX;
    case SBUNIX_SC_CLK_TCK:           return SBUNIX_CLK_TCK;
    case SBUNIX_SC_PAGESIZE:          return SBUNIX_PAGESIZE;   /* _SC_PAGE_SIZE == _SC_PAGESIZE == 6 */
    case SBUNIX_SC_ARG_MAX:           return SBUNIX_ARG_MAX;
    case SBUNIX_SC_NGROUPS_MAX:       return SBUNIX_NGROUPS_MAX;
    case SBUNIX_SC_LOGIN_NAME_MAX:    return SBUNIX_LOGIN_NAME_MAX;
    case SBUNIX_SC_SIGQUEUE_MAX:      return SBUNIX_NSIG - 1;
    case SBUNIX_SC_STREAM_MAX:        return SBUNIX_OPEN_MAX;   /* stdio streams ≤ OPEN_MAX */
    case SBUNIX_SC_NPROCESSORS_CONF:  return 1;   /* single-core QEMU */
    case SBUNIX_SC_NPROCESSORS_ONLN:  return 1;
    case SBUNIX_SC_PHYS_PAGES:        return (0x88000000UL - 0x80000000UL) / SBUNIX_PAGESIZE;
    case SBUNIX_SC_HOST_NAME_MAX:     return SBUNIX_HOST_NAME_MAX;
    case SBUNIX_SC_SYMLOOP_MAX:       return SBUNIX_SYMLOOP_MAX;
    case SBUNIX_SC_VERSION:           return 200809L;
    default:
        return -EINVAL;
    }
}

int sys_getrlimit(int resource, struct rlimit *user_rlim) {
    if (resource < 0 || resource >= RLIM_NLIMITS) return -EINVAL;
    return copy_to_user(user_rlim, &current->rlimits[resource],
                        sizeof(struct rlimit));
}

int sys_setrlimit(int resource, const struct rlimit *user_rlim) {
    if (resource < 0 || resource >= RLIM_NLIMITS) return -EINVAL;
    struct rlimit new_rlim;
    if (copy_from_user(&new_rlim, user_rlim, sizeof(struct rlimit)) < 0)
        return -EFAULT;
    /* Unprivileged: may not raise soft limit above hard limit. */
    if (new_rlim.rlim_cur > current->rlimits[resource].rlim_max) return -EPERM;
    /* May not raise hard limit (no CAP_SYS_RESOURCE). */
    if (new_rlim.rlim_max > current->rlimits[resource].rlim_max) return -EPERM;
    current->rlimits[resource] = new_rlim;
    return 0;
}

/*
 * sys_pathconf_query -- shared helper for pathconf(2) and fpathconf(2).
 *
 * SBUnix uses fixed, filesystem-independent limits, so the path/fd argument
 * only needs to be validated for existence; the returned value is the same
 * for every path or fd.  Returns the limit value (>= 0) or -EINVAL for
 * unknown names.
 *
 * _PC_* values match the Linux POSIX ABI (same as the libc/include/unistd.h
 * definitions).
 */
static long pathconf_query(int name) {
    switch (name) {
    case 0:  /* _PC_LINK_MAX */         return SBUNIX_LINK_MAX;
    case 1:  /* _PC_MAX_CANON */        return SBUNIX_MAX_CANON;
    case 2:  /* _PC_MAX_INPUT */        return SBUNIX_MAX_INPUT;
    case 3:  /* _PC_NAME_MAX */         return SBUNIX_NAME_MAX;
    case 4:  /* _PC_PATH_MAX */         return SBUNIX_PATH_MAX;
    case 5:  /* _PC_PIPE_BUF */         return SBUNIX_PIPE_BUF;
    case 6:  /* _PC_CHOWN_RESTRICTED */ return 0;   /* no multi-user; chown unrestricted */
    case 7:  /* _PC_NO_TRUNC */         return 1;   /* long names return ENAMETOOLONG */
    case 8:  /* _PC_VDISABLE */         return 0;   /* no terminal disable character */
    case 9:  /* _PC_SYNC_IO */          return 1;
    case 10: /* _PC_ASYNC_IO */         return -1;  /* not supported; -1 = indeterminate */
    case 11: /* _PC_PRIO_IO */          return -1;  /* not supported */
    case 13: /* _PC_FILESIZEBITS */     return 32;  /* ext2 max file size: 2 GB */
    case 19: /* _PC_SYMLINK_MAX */      return SBUNIX_SYMLINK_MAX;
    case 20: /* _PC_2_SYMLINKS */       return 1;   /* symbolic links are supported */
    default:
        return -EINVAL;
    }
}

/*
 * pathconf(2): query a filesystem limit by pathname.
 * The path must resolve to an existing entry; the returned value is the same
 * regardless of which filesystem the path resides on (SBUnix uses fixed limits).
 */
long sys_pathconf(const char *upath, int name) {
    char *kpath = kmalloc(MAX_PATH);
    if (!kpath) return -ENOMEM;
    if (strncpy_from_user(kpath, upath, MAX_PATH) < 0) { kfree(kpath); return -EFAULT; }
    kpath[MAX_PATH - 1] = '\0';

    dentry_t *de = NULL;
    dentry_t *start = (current && current->fs) ? current->fs->cwd : NULL;
    int rc = vfs_lookup_from(start, kpath, &de);
    if (rc < 0 || !de) { kfree(kpath); return rc < 0 ? rc : -ENOENT; }
    dentry_put(de);
    kfree(kpath);

    return pathconf_query(name);
}

int sys_sigreturn(trap_frame *frame) {
    if (!current) return -1;
    struct sigframe sf;
    uintptr_t sp = frame->sp;
    if (copy_from_user(&sf, (void *)sp, sizeof(sf)) < 0) return -EFAULT;

    /* Restore the full interrupted register context. */
    frame->sepc  = sf.sepc;
    frame->ra    = sf.ra;
    frame->gp    = sf.gp;
    frame->tp    = sf.tp;
    frame->t0 = sf.t[0]; frame->t1 = sf.t[1]; frame->t2 = sf.t[2];
    frame->t3 = sf.t[3]; frame->t4 = sf.t[4]; frame->t5 = sf.t[5];
    frame->t6 = sf.t[6];
    frame->s0  = sf.s[0];  frame->s1  = sf.s[1];  frame->s2  = sf.s[2];
    frame->s3  = sf.s[3];  frame->s4  = sf.s[4];  frame->s5  = sf.s[5];
    frame->s6  = sf.s[6];  frame->s7  = sf.s[7];  frame->s8  = sf.s[8];
    frame->s9  = sf.s[9];  frame->s10 = sf.s[10]; frame->s11 = sf.s[11];
    frame->a0 = sf.a[0]; frame->a1 = sf.a[1]; frame->a2 = sf.a[2];
    frame->a3 = sf.a[3]; frame->a4 = sf.a[4]; frame->a5 = sf.a[5];
    frame->a6 = sf.a[6]; frame->a7 = sf.a[7];
    frame->sp  = sf.sp;   /* restore original sp saved before signal delivery */

    /* Restore signal mask; strip unmaskable bits for safety. */
    current->signal_mask = sf.saved_mask & ~((1U << KERNEL_SIGKILL)  | (1U << KERNEL_SIGSTOP)  |
                                              (1U << KERNEL_SIGILL)   | (1U << KERNEL_SIGABRT)  |
                                              (1U << KERNEL_SIGTRAP)  | (1U << KERNEL_SIGBUS));
    return 0;   /* return value unused: caller returns early before frame->a0 = ret */
}

/*
 * fpathconf(2): query a filesystem limit by open file descriptor.
 * The fd must be open; the returned value is the same for every file.
 */
long sys_fpathconf(int fd, int name) {
    if (fd < 0 || fd >= MAX_FDS) return -EBADF;
    if (!current || !current->fds.files[fd]) return -EBADF;
    return pathconf_query(name);
}

int sys_uname(void *buf) {
    if (!buf) return -EFAULT;

    // struct utsname: five char[65] fields — sysname, nodename, release, version, machine
    char ku[5][65];
    __builtin_memset(ku, 0, sizeof(ku));
    strncpy(ku[0], "SBUnix",  64);
    strncpy(ku[1], "sbunix",  64);
    strncpy(ku[2], "0.1.0",   64);
    strncpy(ku[3], "#1",      64);
    strncpy(ku[4], "riscv64", 64);

    if (copy_to_user(buf, ku, sizeof(ku)) < 0) return -EFAULT;
    return 0;
}
