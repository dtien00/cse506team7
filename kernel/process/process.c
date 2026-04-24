#include "process/process.h"
#include "kernel/printk.h"
#include "signal.h"
#include "trap/trap.h"
#include "arch/riscv.h"
#include "fs/vfs.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"   // needed for kmalloc/kfree in exec()
#include "mm/vma.h"
#include "types.h"
#include "lib/string.h"
#include "elf/elf.h"
#include "fs/file.h"
#include "process/wait.h"

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

static uint32_t next_pid = 1;

/* Forward declaration — defined later in this file. */
void proc_start_user(struct proc *p, uint64_t entry, uint64_t user_sp);

/**
 * Default entry point for new kernel threads.
 * When swtch restores ra to this function, the new process starts here.
 * If exec() has already loaded a user image (current->pc != 0), transfer
 * control to user mode via sret; otherwise spin waiting for work.
 */
static void proc_entry(void) {
    interrupts_on();
    printk("[proc %d] started\n", current->pid);
    if (current->pc != 0)
        proc_start_user(current, current->pc, current->sp);
    while (1)
        asm volatile("wfi");
}

struct proc* proc_alloc(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proctab[i].state == PROC_UNUSED) {
            memset(&proctab[i], 0, sizeof(proctab[i]));
            proctab[i].state = PROC_CREATED;   // mark as taken immediately
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

    // next_pid is 1 at file scope, so proc_create assigns pid=1 to init
    struct proc *init = proc_create("init");
    next_pid = 2;  // next real process gets pid 2
    init->priority = 0;  // PID 1 runs at highest priority — never starved

    // proc_create leaves fds and cwd/pwd in fs to the caller
    fd_init(&init->fds);

    init->fs = fs_struct_alloc();
    fs_struct_set_root(init->fs, "/");   // set where "/" is
    fs_struct_set_cwd(init->fs, "/");    // set where we are
    // Prepare argv for /bin/init
    static char *argv[] = { "init", NULL };
    // Set current to init so exec uses the right process
    current = init;
    if (exec("/bin/init", argv, NULL) < 0) {
        printk("[init] exec failed for /bin/init, falling back to /bin/sh\n");
        static char *sh_argv[] = { "sh", NULL };
        if (exec("/bin/sh", sh_argv, NULL) < 0)
            printk("[init] exec failed fallback to /bin/sh\n");
    }
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

    memset(p, 0, sizeof(*p));

    p->pid = next_pid++;
    /* Each new process starts as its own group and session leader.
     * sys_fork() overwrites these with the parent's pgid/sid. */
    p->pgid = p->pid;
    p->sid  = p->pid;
    strncpy(p->name, name, MAX_NAME - 1);
    p->state = PROC_CREATED;

    void *pages = pmm_alloc_pages(2);
    if (!pages) return NULL;
    p->kguard = (uint8_t *)pages;
    p->kstack = (uint8_t *)pages + KSTACK_SIZE;
    vmm_unmap(vmm_kernel_pagetable(), (uintptr_t)p->kguard);
    asm volatile("sfence.vma %0, zero" :: "r"((uintptr_t)p->kguard));

    p->context.sp = (uint64_t)(p->kstack + KSTACK_SIZE);
    p->context.ra = (uint64_t)proc_entry;
    p->priority = PROC_DEFAULT_PRIORITY;
    p->mlfq_level = 0;
    p->trapframe = NULL;
    p->trapframe_owned = 0;
    for (int i = 0; i < MAX_HANDLERS; i++) { // Initialize all entries to SIG_DFL (0)
        p->handlers[i] = 0;
    }

    // Initialize VMA list
    vma_init(&p->vma_list);

    // Caller finishes initialization (address space, trapframe, fds)
    // and then decides when to enqueue via proc_ready().
    return p;
}

/* Free just the address space — used by exec() to drop the old image */
static void proc_free_pagetable(struct proc *p) {
    if (p->pagetable) {
        vmm_free_pagetable(p->pagetable);
        p->pagetable = NULL;
    }
}

/* Full teardown — used by proc_exit().
 * Does NOT free the page table: the zombie must remain mapped until the
 * parent calls waitpid() and proc_free() reaps it.  Freeing the page table
 * here would return its physical pages to the PMM while the scheduler may
 * still switch to them (e.g. for a sibling process). */
void proc_destroy(struct proc *p) {
    if (!p) return;

    proc_free_pagetable(p);

    if (p->trapframe && p->trapframe_owned) {
        kfree(p->trapframe);
    }
    p->trapframe = NULL;
    p->trapframe_owned = 0;

    if (p->fs) {
        fs_struct_free(p->fs);
        p->fs = NULL;
    }

    fd_destroy(&p->fds);
}

void proc_free(struct proc *p) {
    if (!p) return;

    // Free VMA list
    vma_free_all(p->vma_list);

    if (p->kguard) {
        vmm_map(vmm_kernel_pagetable(), (uintptr_t)p->kguard,
                (uintptr_t)p->kguard, PTE_R | PTE_W);
        asm volatile("sfence.vma %0, zero" :: "r"((uintptr_t)p->kguard));
        pmm_free_page(p->kguard);
        pmm_free_page(p->kstack);
        p->kguard = NULL;
        p->kstack = NULL;
    }

    memset(p, 0, sizeof(*p));
    p->state = PROC_UNUSED;
}

void proc_exit(int code) {
    if (!current) return;

    proc_destroy(current); // to protect from memory leaks

    // Store exit code and mark as ZOMBIE
    current->exit_code = code;
    current->state = PROC_ZOMBIE;

    // Wake parent if it exists (so it can call wait)
    if (current->parent)
        proc_wakeup(current->parent);

    // Reparent children to init (pid 1) to prevent orphans
    struct proc *initproc = proc_find(1);
    struct proc *child = current->first_child;
    while (child) {
        struct proc *next_sib = child->sibling;
        child->parent = initproc;
        child->sibling = initproc->first_child;
        initproc->first_child = child;
        child = next_sib;
    }
    current->first_child = NULL;

    if (current->parent) {
        current->parent->pending_signals |= (1u << SIGCHLD);
        // Only wake the parent's pending_signals channel if the parent has
        // installed a non-default handler for SIGCHLD.  When the disposition
        // is SIG_DFL, SIGCHLD's default action is "ignore" (POSIX) and the
        // signal will be silently cleared at the next dispatch point.
        // This prevents spurious wakeups of pause()/sigsuspend().
        if (current->parent->handlers[SIGCHLD] != (void *)0 /* SIG_DFL */)
            proc_wakeup(&current->parent->pending_signals);
    }

    // Switch back via the normal yield path so the current process context
    // is saved in its own slot; schedule() must run on scheduler_context.
    proc_yield();

    // proc_exit is noreturn in practice.
    while (1)
        asm volatile("wfi");
}

void proc_kill(struct proc *p) {
    if (p) p->state = PROC_ZOMBIE;
}


// Wait for a child to exit. Returns child's PID, or -1 if no children.
// If exit_code_out is non-NULL, stores the child's exit code there.
int proc_wait(int *exit_code_out) {
    if (!current) return -1;
    struct proc *child = current->first_child;
    while (1) {
        int have_children = 0;
        child = current->first_child;
        while (child) {
            have_children = 1;
            if (child->state == PROC_ZOMBIE) {
                int pid = child->pid;
                // Remove child from parent's child list
                struct proc **link = &current->first_child;
                while (*link && *link != child) link = &(*link)->sibling;
                if (*link == child) *link = child->sibling;
                // Write exit code to user-space pointer with SUM so S-mode
                // can access user pages.
                if (exit_code_out) {
                    set_csr_bit(sstatus, SSTATUS_SUM);
                    if (child->exit_code < 0)
                        *exit_code_out = (-child->exit_code) & 0x7f;
                    else
                        *exit_code_out = (child->exit_code & 0xff) << 8;
                    clear_csr_bit(sstatus, SSTATUS_SUM);
                }

                // Accumulate child CPU time into parent before freeing
                current->children_utime += child->utime_ticks;

                proc_free(child);
                return pid;
            }
            child = child->sibling;
        }
        if (!have_children) return -1;
        // Block until a child exits.  proc_sleep puts the process into
        // blocked_queue (via proc_block) so that proc_wakeup() inside
        // proc_exit() can find and unblock it.  The channel is the current
        // process pointer itself; proc_exit calls proc_wakeup(child->parent)
        // which matches any caller that slept on that same address.
        proc_sleep(current);
    }
}

// Wait for a specific child pid to exit.  Returns child's PID on success,
// or -1 if the target pid is not a child of the current process.
// options: 1 = WNOHANG (return 0 immediately if no qualifying child found).
//          2 = WUNTRACED / 8 = WCONTINUED: accepted but no-op until SIGSTOP/SIGCONT are implemented.
int proc_waitpid(pid_t target_pid, int *exit_code_out, int options) {
    if (!current) return -1;

    // POSIX pid semantics:
    //   -1      → any child
    //   > 0     → specific child pid
    //   0       → any child in caller's process group
    //   < -1    → any child whose pgid == abs(pid)
    // match_mode: 0 = specific pid, 1 = pgid match, 2 = any child
    int match_mode = 0;
    pid_t target_pgid = 0;
    if (target_pid == -1) {
        match_mode = 2;  // any child
    } else if (target_pid < -1) {
        match_mode = 1;
        target_pgid = -target_pid;
    } else if (target_pid == 0) {
        match_mode = 1;
        target_pgid = current->pgid;
    }

    while (1) {
        int have_target = 0;
        struct proc *child = current->first_child;
        while (child) {
            int matches;
            switch (match_mode) {
                case 2:  matches = 1; break;                          // any
                case 1:  matches = (child->pgid == target_pgid); break; // pgid
                default: matches = (child->pid == target_pid); break;   // specific
            }
            if (matches) {
                have_target = 1;
                if (child->state == PROC_ZOMBIE) {
                    struct proc **link = &current->first_child;
                    while (*link && *link != child) link = &(*link)->sibling;
                    if (*link == child) *link = child->sibling;
                    if (exit_code_out) {
                        set_csr_bit(sstatus, SSTATUS_SUM);
                        if (child->exit_code < 0)
                            *exit_code_out = (-child->exit_code) & 0x7f;
                        else
                            *exit_code_out = (child->exit_code & 0xff) << 8;
                        clear_csr_bit(sstatus, SSTATUS_SUM);
                    }
                    current->children_utime += child->utime_ticks;
                    pid_t ret = child->pid;
                    proc_free(child);
                    return ret;
                }
                // WUNTRACED: report PROC_STOPPED children
                if ((options & WUNTRACED) && child->state == PROC_STOPPED && child->stop_unreported) {
                    child->stop_unreported = 0;
                    if (exit_code_out) {
                        set_csr_bit(sstatus, SSTATUS_SUM);
                        int ss = child->stop_signal ? child->stop_signal : SIGSTOP;
                        *exit_code_out = (ss << 8) | 0x7f;
                        clear_csr_bit(sstatus, SSTATUS_SUM);
                    }
                    return child->pid;
                }
                // WCONTINUED: report continued children
                if ((options & WCONTINUED) && child->cont_unreported) {
                    child->cont_unreported = 0;
                    if (exit_code_out) {
                        set_csr_bit(sstatus, SSTATUS_SUM);
                        *exit_code_out = 0xffff;
                        clear_csr_bit(sstatus, SSTATUS_SUM);
                    }
                    return child->pid;
                }
                // For multi-child matches, keep scanning siblings
                if (match_mode == 0) break;
            }
            child = child->sibling;
        }
        if (!have_target) return -1;
        if (options & 1) return 0;    // WNOHANG
        proc_sleep(current);
    }
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
                case PROC_STOPPED:   state_str = "STOPPED"; break;
                default:            state_str = "UNKNOWN"; break;
            }
            printk("%3d  %-9s  %s\n", proctab[i].pid, state_str, proctab[i].name);
        }
    }
}

void fork_ret(void) {
    struct proc *p = current;
    trap_frame *tf = p->trapframe;

    // Mirror proc_start_user: kernel stack in sscratch, U-mode sstatus, switch pagetable
    write_csr(sscratch, (uint64_t)(p->kstack + KSTACK_SIZE));
    write_csr(sepc, tf->sepc);
    write_csr(sstatus, (tf->sstatus & ~SSTATUS_SPP) | SSTATUS_SPIE);
    vmm_switch(p->pagetable);

    // Restore all 31 GPRs from the trapframe and sret.
    // t6 is used as the pointer, so it must be loaded last.
    register uint64_t tf_r asm("t6") = (uint64_t)tf;
    asm volatile(
        "ld ra,    0(t6)\n"
        "ld sp,    8(t6)\n"
        "ld gp,   16(t6)\n"
        "ld tp,   24(t6)\n"
        "ld a0,   32(t6)\n"
        "ld a1,   40(t6)\n"
        "ld a2,   48(t6)\n"
        "ld a3,   56(t6)\n"
        "ld a4,   64(t6)\n"
        "ld a5,   72(t6)\n"
        "ld a6,   80(t6)\n"
        "ld a7,   88(t6)\n"
        "ld s0,   96(t6)\n"
        "ld s1,  104(t6)\n"
        "ld s2,  112(t6)\n"
        "ld s3,  120(t6)\n"
        "ld s4,  128(t6)\n"
        "ld s5,  136(t6)\n"
        "ld s6,  144(t6)\n"
        "ld s7,  152(t6)\n"
        "ld s8,  160(t6)\n"
        "ld s9,  168(t6)\n"
        "ld s10, 176(t6)\n"
        "ld s11, 184(t6)\n"
        "ld t0,  192(t6)\n"
        "ld t1,  200(t6)\n"
        "ld t2,  208(t6)\n"
        "ld t3,  216(t6)\n"
        "ld t4,  224(t6)\n"
        "ld t5,  232(t6)\n"
        "ld t6,  240(t6)\n"
        "sret\n"
        : : "r"(tf_r) : "memory"
    );
    __builtin_unreachable();
}

void proc_start_user(struct proc *p, uint64_t entry, uint64_t user_sp) {
    // SPP=0 → sret returns to U-mode; SPIE=1 → interrupts enabled after sret
    write_csr(sstatus, (read_csr(sstatus) & ~SSTATUS_SPP) | SSTATUS_SPIE);
    write_csr(sepc, entry);

    // Point sscratch at this process's kernel stack top so the next
    // trap from U-mode can find the kernel stack.
    write_csr(sscratch, (uint64_t)(p->kstack + KSTACK_SIZE));

    // Switch to the process's page table.
    // Kernel mappings are shared in every page table, so this is safe.
    vmm_switch(p->pagetable);

    // Load argument registers (set by exec) then switch stack and sret.
    // Using explicit register variables ensures GCC places values in the
    // correct architectural registers before the sret instruction fires.
    register uint64_t a0_r asm("a0") = p->regs[10]; /* argc */
    register uint64_t a1_r asm("a1") = p->regs[11]; /* argv */
    register uint64_t a2_r asm("a2") = p->regs[12]; /* envp */
    /* a0_r/a1_r/a2_r are not referenced by %N in the template, but GCC is
     * required to hold named register variables in their bound register for
     * the lifetime of the asm block.  They will be live in a0/a1/a2 at sret,
     * giving userspace argc/argv/envp in the standard RISC-V calling convention. */
    asm volatile(
        "mv sp, %2\n"
        "sret\n"
        :
        : "r"(a0_r), "r"(a1_r), "r"(user_sp), "r"(a2_r)
        : "memory"
    );
    __builtin_unreachable();
}

/**
 * build_user_stack - Construct the initial process stack in two passes.
 *
 * Pass 1: Measure total frame size (strings + pointer array + alignment).
 *         Compute final sp once — no adjustments after this point.
 * Pass 2: Write strings then pointer/metadata region at known offsets.
 *
 * Stack layout (addresses increase upward):
 *   sp+0                        : argc (uintptr_t)
 *   sp+8                        : argv[0] VA
 *   ...
 *   sp+(argc+1)*8               : NULL  (argv terminator)
 *   sp+(argc+2)*8               : envp[0] VA
 *   ...
 *   sp+(argc+envc+2)*8          : NULL  (envp terminator)
 *   sp+ptrs_bytes .. stack_top  : NUL-terminated strings
 *
 * Returns 0 on success, -1 on any failure.
 */
static int build_user_stack(pagetable_t pt, uintptr_t stack_top,
                            char *const argv[], char *const envp[],
                            uintptr_t *sp_out, uintptr_t *argv_out,
                            uintptr_t *envp_out, int *argc_out) {
    /* --- Pass 1: measure --- */
    int argc = 0, envc = 0;
    size_t argv_str_bytes = 0, envp_str_bytes = 0;

    while (argv && argv[argc]) { argv_str_bytes += strlen(argv[argc]) + 1; argc++; }
    while (envp && envp[envc]) { envp_str_bytes += strlen(envp[envc]) + 1; envc++; }

    /* Pointer region: 1 word (argc) + argc words (argv ptrs) + 1 NULL
     *               + envc words (envp ptrs) + 1 NULL              */
    size_t ptrs_bytes    = (size_t)(1 + argc + 1 + envc + 1) * sizeof(uintptr_t);
    size_t strings_bytes = argv_str_bytes + envp_str_bytes;
    size_t total         = ptrs_bytes + strings_bytes;
    total                = (total + 15u) & ~(size_t)15u; /* round up to 16 */

    /* Sanity: must fit in the single pre-allocated stack page */
    if (total > PGSIZE)
        return -1;

    /* stack_top (KERNBASE) is page-aligned (multiple of 16), and total is a
     * multiple of 16, so sp is guaranteed 16-byte aligned.               */
    uintptr_t sp = stack_top - total;

    /* --- Pass 2: write --- */
    uintptr_t *argv_va = (uintptr_t *)kmalloc((size_t)(argc + 1) * sizeof(uintptr_t));
    if (!argv_va) return -1;
    uintptr_t *envp_va = (uintptr_t *)kmalloc((size_t)(envc + 1) * sizeof(uintptr_t));
    if (!envp_va) { kfree(argv_va); return -1; }

    /* String region starts right after the pointer region */
    uintptr_t str_cursor = sp + ptrs_bytes;

    /* Safe to memcpy from a single vmm_translate per string: the total frame
     * size was checked against PGSIZE above, so the entire frame — strings
     * included — fits within one physical page. No string crosses a page
     * boundary.                                                             */
    /* Write argv strings */
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        uintptr_t pa = vmm_translate(pt, str_cursor);
        if (!pa) { kfree(envp_va); kfree(argv_va); return -1; }
        memcpy((void *)pa, argv[i], len);
        argv_va[i] = str_cursor;
        str_cursor += len;
    }
    argv_va[argc] = 0;

    /* Same single-page invariant holds for envp strings. */
    /* Write envp strings */
    for (int i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1;
        uintptr_t pa = vmm_translate(pt, str_cursor);
        if (!pa) { kfree(envp_va); kfree(argv_va); return -1; }
        memcpy((void *)pa, envp[i], len);
        envp_va[i] = str_cursor;
        str_cursor += len;
    }
    envp_va[envc] = 0;

#define STACK_WRITE(va, val) do {                               \
    uintptr_t _pa = vmm_translate(pt, (uintptr_t)(va));         \
    if (!_pa) { kfree(envp_va); kfree(argv_va); return -1; }   \
    *(uintptr_t *)_pa = (uintptr_t)(val);                       \
} while (0)

    uintptr_t user_argv = sp + sizeof(uintptr_t);
    uintptr_t user_envp = user_argv + (uintptr_t)(argc + 1) * sizeof(uintptr_t);

    /* Each pointer slot is 8 bytes and 8-byte-aligned; since the entire frame
     * fits in one physical page (verified by total > PGSIZE guard), no slot
     * crosses a page boundary. STACK_WRITE's single vmm_translate is safe.  */
    STACK_WRITE(sp, (uintptr_t)argc);

    for (int i = 0; i <= argc; i++)
        STACK_WRITE(user_argv + (uintptr_t)i * sizeof(uintptr_t), argv_va[i]);

    for (int i = 0; i <= envc; i++)
        STACK_WRITE(user_envp + (uintptr_t)i * sizeof(uintptr_t), envp_va[i]);

#undef STACK_WRITE

    kfree(argv_va);
    kfree(envp_va);

    *sp_out   = sp;
    *argv_out = user_argv;
    *envp_out = user_envp;
    *argc_out = argc;
    return 0;
}

int exec(const char *path, char *const argv[], char *const envp[]) {
    // 1. Open the binary
    int fd = file_open(path);
    if (fd < 0) return -1;

    // 2. Read and validate the ELF64 header
    elf_header_t ehdr;
    if (file_read(fd, &ehdr, sizeof(ehdr)) != (int)sizeof(ehdr))
        goto fail_file;
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F' ||
        ehdr.e_ident[4] != 2 /* ELFCLASS64 */) {
        goto fail_file;
    }

    // 3. Create a fresh page table and VMA list for the new image
    pagetable_t new_pt = vmm_create_pagetable();
    if (!new_pt) goto fail_file;
    struct vm_area *new_vma_list = NULL;
    vma_init(&new_vma_list);

    uintptr_t highest_seg_end = 0;

    // 4. Load each PT_LOAD segment into the new page table
    for (int i = 0; i < ehdr.e_phnum; i++) {
        elf_prog_header_t phdr;
        if (file_lseek(fd, (size_t)(ehdr.e_phoff + (uint64_t)i * sizeof(phdr)), SEEK_SET) < 0)
            goto fail_pt;
        if (file_read(fd, &phdr, sizeof(phdr)) != (int)sizeof(phdr))
            goto fail_pt;
        if (phdr.p_type != PT_LOAD)
            continue;

        // Derive PTE permission bits from ELF segment flags
        uint64_t perm = PTE_U;
        if (phdr.p_flags & PF_R) perm |= PTE_R;
        if (phdr.p_flags & PF_W) perm |= PTE_W;
        if (phdr.p_flags & PF_X) perm |= PTE_X;

        uintptr_t seg_start = PGROUNDDOWN(phdr.p_vaddr);
        uintptr_t seg_end   = PGROUNDUP(phdr.p_vaddr + phdr.p_memsz);

        if (seg_end > highest_seg_end) {
            highest_seg_end = seg_end;
        }

        // Allocate and map physical pages; zero them so BSS is handled
        for (uintptr_t va = seg_start; va < seg_end; va += PGSIZE) {
            void *pg = pmm_alloc_page();
            if (!pg) goto fail_pt;
            memset(pg, 0, PGSIZE);
            if (vmm_map(new_pt, va, (uintptr_t)pg, perm) < 0) {
                pmm_free_page(pg);
                goto fail_pt;
            }
        }

        // Seek to segment data in file, then copy into the mapped PA range
        if (file_lseek(fd, (size_t)phdr.p_offset, SEEK_SET) < 0)
            goto fail_vma;
        size_t remaining = (size_t)phdr.p_filesz;
        uintptr_t va     = phdr.p_vaddr;
        while (remaining > 0) {
            // Translate user VA → physical address (identity-mapped in kernel)
            uintptr_t pa = vmm_translate(new_pt, va);
            if (!pa) goto fail_vma;
            size_t chunk = PGSIZE - (va & (PGSIZE - 1));
            if (chunk > remaining) chunk = remaining;
            if (file_read(fd, (void *)pa, chunk) != (int)chunk)
                goto fail_vma;
            va        += chunk;
            remaining -= chunk;
        }
        // p_memsz > p_filesz (BSS) is already zero from memset above
        // Create VMA for this segment
        uint32_t vma_prot = 0;
        if (phdr.p_flags & PF_R) vma_prot |= VMA_PROT_READ;
        if (phdr.p_flags & PF_W) vma_prot |= VMA_PROT_WRITE;
        if (phdr.p_flags & PF_X) vma_prot |= VMA_PROT_EXEC;
        struct vm_area *seg_vma = vma_create(seg_start, seg_end, vma_prot, VMA_PRIVATE, VMA_TYPE_FILE);
        if (!seg_vma) goto fail_vma;
        if (vma_insert(&new_vma_list, seg_vma) != 0) {
            vma_free(seg_vma);
            goto fail_vma;
        }
    }

    // 5a. Reserve kernel device regions as zero-permission VMAs.
    // vmm_create_pagetable copies the kernel's device PTEs (no PTE_U) into every
    // user page table for low VA addresses.  Without VMAs covering those ranges,
    // vma_find_free_region is blind to them and can hand out addresses that overlap
    // kernel identity mappings, causing PROT FAULTs on demand-page attempts.
    // PROT=0 ensures any user access is caught as a protection violation.
    {
        static const struct { uintptr_t start; uintptr_t end; } dev_regions[] = {
            { 0x00101000, 0x00102000 },  /* Goldfish RTC           */
            { 0x0C000000, 0x10010000 },  /* PLIC + UART (contiguous) */
            { 0x30000000, 0x31000000 },  /* PCIe ECAM              */
            { 0x40000000, 0x50000000 },  /* PCI MMIO BAR window    */
            { KERNBASE, ((uintptr_t)(KERNBASE >> 30) + 1) << 30 },  /* Full kernel L2 VA range    */
        };
        for (size_t di = 0; di < sizeof(dev_regions)/sizeof(dev_regions[0]); di++) {
            struct vm_area *dv = vma_create(dev_regions[di].start, dev_regions[di].end,
                                            0, VMA_PRIVATE, VMA_TYPE_DEVICE);
            if (!dv) goto fail_vma;
            if (vma_insert(&new_vma_list, dv) != 0) {
                vma_free(dv);
                goto fail_vma;
            }
        }
    }

    // 5. Allocate and map user stack (just below KERNBASE)
    // Only pre-allocate the topmost page (where argv strings are placed).
    // Deeper stack frames are demand-allocated by the page fault handler
    // via VMA_GROWSDOWN, up to USTACK_SIZE (1 MB) total.
    uintptr_t stack_top          = KERNBASE;
    uintptr_t initial_stack_base = stack_top - PGSIZE;

    {
        void *pg = pmm_alloc_page();
        if (!pg) goto fail_vma;
        memset(pg, 0, PGSIZE);
        if (vmm_map(new_pt, initial_stack_base, (uintptr_t)pg, PTE_R | PTE_W | PTE_U) < 0) {
            pmm_free_page(pg);
            goto fail_vma;
        }
    }

    // VMA starts at the single pre-allocated page; grows down on fault.
    // vma->end (KERNBASE) is the anchor used by vma_find_growsdown() to
    // compute the floor: end - USTACK_SIZE = 0x7FF00000.
    struct vm_area *stack_vma = vma_create(initial_stack_base, stack_top,
                                           VMA_PROT_READ | VMA_PROT_WRITE,
                                           VMA_PRIVATE | VMA_GROWSDOWN,
                                           VMA_TYPE_STACK);
    if (!stack_vma) goto fail_vma;
    if (vma_insert(&new_vma_list, stack_vma) != 0) {
        vma_free(stack_vma);
        goto fail_vma;
    }

    // 6. Build user stack: argc/argv/envp strings and pointer arrays.
    int argc;
    uintptr_t sp = 0;
    uintptr_t user_argv, user_envp;
    // printk("gonna build the userspace process stack.\n");
    // printk("argv[0]: %s", argv[0]);
    if (build_user_stack(new_pt, stack_top, argv, envp,
                         &sp, &user_argv, &user_envp, &argc) < 0)
        goto fail_vma;

    // 7. Commit: install new address space, then tear down the old one.
    // Keep the old root alive until after SATP switches away from it.
    // NOTE: current->priority (nice value) and current->signal_mask are
    // intentionally NOT reset here — POSIX requires both to be preserved
    // across exec().  Do not clear these fields in future refactors.
    // Signal dispositions: SIG_IGN survives exec(); all others reset to SIG_DFL.
    for (int i = 0; i < 32; i++) {
        if (current->handlers[i] != (void *)SIG_IGN)
            current->handlers[i] = (void *)SIG_DFL;
    }
    pagetable_t old_pt = current->pagetable;
    struct vm_area *old_vma_list = current->vma_list;

    current->pagetable = new_pt;
    current->vma_list = new_vma_list;
    current->sz        = highest_seg_end;
    current->satp      = MAKE_SATP((uintptr_t)new_pt);
    
    // Reset MLFQ scheduling state so the new image starts fresh.
    current->mlfq_level = 0;
    current->time_slice = 1;   // 1 << 0
    current->total_time = 0;

    // Switch to the new page table before freeing old page-table pages.
    vmm_switch(current->pagetable);

    vmm_free_pagetable(old_pt);
    vma_free_all(old_vma_list);

    // 8. Write entry point and argument registers into the live trapframe
    //    (set by syscall_handler before calling exec; NULL at boot-time)
    if (current->trapframe) {
        current->trapframe->sepc    = ehdr.e_entry;
        current->trapframe->sstatus = (current->trapframe->sstatus & ~SSTATUS_SPP)
                                      | SSTATUS_SPIE;
        current->trapframe->sp      = sp;
        current->trapframe->a0      = (uint64_t)argc;
        current->trapframe->a1      = user_argv;
        current->trapframe->a2      = user_envp;
    }
    // Fallback proc fields for boot-time exec (no trapframe yet)
    current->pc       = ehdr.e_entry;
    current->sp       = sp;
    current->regs[10] = (uint64_t)argc;
    current->regs[11] = user_argv;
    current->regs[12] = user_envp;

    // current->pagetable is already active after commit above.

    file_close(fd);
    return 0;

fail_vma:
    vma_free_all(new_vma_list);
fail_pt:
    vmm_free_pagetable(new_pt);
fail_file:
    file_close(fd);
    return -1;
}
