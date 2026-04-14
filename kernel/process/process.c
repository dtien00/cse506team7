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
    if (exec("/bin/init", argv) < 0) {
        printk("[init] exec failed for /bin/init, falling back to /bin/sh\n");
        static char *sh_argv[] = { "sh", NULL };
        if (exec("/bin/sh", sh_argv) < 0)
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

    int idx = p - proctab;
    memset(p, 0, sizeof(*p));

    p->pid = next_pid++;
    /* Each new process starts as its own group and session leader.
     * sys_fork() overwrites these with the parent's pgid/sid. */
    p->pgid = p->pid;
    p->sid  = p->pid;
    strncpy(p->name, name, MAX_NAME - 1);
    p->state = PROC_CREATED;
    p->context.sp = (uint64_t)&kstacks[idx][KSTACK_SIZE];
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
    // POSIX: pid == -1 means wait for any child.
    if (target_pid == -1)
        return proc_wait(exit_code_out);

    if (!current) return -1;
    while (1) {
        int have_target = 0;
        struct proc *child = current->first_child;
        while (child) {
            if (child->pid == target_pid) {
                have_target = 1;
                if (child->state == PROC_ZOMBIE) {
                    // Remove from parent's child list
                    struct proc **link = &current->first_child;
                    while (*link && *link != child) link = &(*link)->sibling;
                    if (*link == child) *link = child->sibling;
                    if (exit_code_out) {
                        set_csr_bit(sstatus, SSTATUS_SUM);
                        // Negative exit_code means killed by signal (POSIX convention):
                        //   signal death → low 7 bits = signal number (WIFSIGNALED)
                        //   normal exit  → high byte  = exit status  (WIFEXITED)
                        if (child->exit_code < 0)
                            *exit_code_out = (-child->exit_code) & 0x7f;
                        else
                            *exit_code_out = (child->exit_code & 0xff) << 8;
                        clear_csr_bit(sstatus, SSTATUS_SUM);
                    }
                    // Accumulate child CPU time into parent before freeing
                    current->children_utime += child->utime_ticks;

                    proc_free(child);
                    return target_pid;
                }
                // WUNTRACED (options & 2): would report PROC_STOPPED children here once SIGSTOP is added.
                // WCONTINUED (options & 8): would report cont_unreported children here once SIGCONT is added.
                break;
            }
            child = child->sibling;
        }
        if (!have_target) return -1;  // not our child
        if (options & 1) return 0;    // WNOHANG: no qualifying child found, don't block
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
    int idx = p - proctab;
    write_csr(sscratch, (uint64_t)&kstacks[idx][KSTACK_SIZE]);
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
    int idx = p - proctab;
    write_csr(sscratch, (uint64_t)&kstacks[idx][KSTACK_SIZE]);

    // Switch to the process's page table.
    // Kernel mappings are shared in every page table, so this is safe.
    vmm_switch(p->pagetable);

    // Load argument registers (set by exec) then switch stack and sret.
    // Using explicit register variables ensures GCC places values in the
    // correct architectural registers before the sret instruction fires.
    register uint64_t a0_r asm("a0") = p->regs[10]; /* argc */
    register uint64_t a1_r asm("a1") = p->regs[11]; /* argv */
    asm volatile(
        "mv sp, %2\n"
        "sret\n"
        :
        : "r"(a0_r), "r"(a1_r), "r"(user_sp)
        : "memory"
    );
    __builtin_unreachable();
}

int exec(const char *path, char *const argv[]) {
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

    // 6. Copy argv strings onto the user stack via physical addresses
    int argc = 0;
    while (argv && argv[argc]) argc++;

    uintptr_t *argv_uvas = (uintptr_t *)kmalloc((size_t)(argc + 1) * sizeof(uintptr_t));
    if (!argv_uvas) goto fail_vma;

    uintptr_t sp = stack_top;
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        uintptr_t pa = vmm_translate(new_pt, sp);
        if (!pa) { kfree(argv_uvas); goto fail_vma; }
        memcpy((void *)pa, argv[i], len);
        argv_uvas[i] = sp;   // user-space VA of this string
    }
    argv_uvas[argc] = 0;     // NULL terminator

    // Align sp to 16 bytes before the pointer arrays.
    sp &= ~(uintptr_t)0xF;

    // Push envp[0] = NULL (empty environment).  This must sit between the
    // argv pointer array and the argv strings so that the standard ABI
    // envp = argv + (argc+1)*8 lands on a NULL, not on the string data.
    sp -= sizeof(uintptr_t);
    {
        uintptr_t envp_pa = vmm_translate(new_pt, sp);
        if (!envp_pa) { kfree(argv_uvas); goto fail_vma; }
        *(uintptr_t *)envp_pa = 0;
    }

    // Push the argv pointer array (argv[0]...argv[argc-1], NULL).
    sp -= (uint64_t)(argc + 1) * sizeof(uintptr_t);
    for (int i = 0; i <= argc; i++) {
        uintptr_t slot_va = sp + (uint64_t)i * sizeof(uintptr_t);
        uintptr_t slot_pa = vmm_translate(new_pt, slot_va);
        if (!slot_pa) { kfree(argv_uvas); goto fail_vma; }
        *(uintptr_t *)slot_pa = argv_uvas[i];
    }
    uintptr_t user_argv = sp;

    // Push argc below the argv array so that sp points to argc on entry.
    // Required by the standard Linux/POSIX _start ABI:
    //   *(sp+0)             = argc
    //   *(sp+8)             = argv[0]          (user_argv)
    //   *(sp + (argc+1)*8)  = NULL             (argv terminator)
    //   *(sp + (argc+2)*8)  = NULL             (envp[0], empty env)
    sp -= sizeof(uintptr_t);
    {
        uintptr_t argc_pa = vmm_translate(new_pt, sp);
        if (!argc_pa) { kfree(argv_uvas); goto fail_vma; }
        *(uintptr_t *)argc_pa = (uintptr_t)argc;
    }
    kfree(argv_uvas);

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
    }
    // Fallback proc fields for boot-time exec (no trapframe yet)
    current->pc       = ehdr.e_entry;
    current->sp       = sp;
    current->regs[10] = (uint64_t)argc;
    current->regs[11] = user_argv;

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
