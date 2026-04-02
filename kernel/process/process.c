#include "process/process.h"
#include "process/swtch.h"
#include "kernel/printk.h"
#include "trap/trap.h"
#include "arch/riscv.h"
#include "fs/vfs.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"   // needed for kmalloc/kfree in exec()
#include "types.h"
#include "string.h"
#include "elf/elf.h"
#include "trap/syscall.h"
#include "fs/file.h"

#ifndef SEEK_SET
#define SEEK_SET 0
#endif


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

    struct proc *init = proc_alloc();
    int idx = init - proctab;
    memset(init, 0, sizeof(*init));
    init->pid = 1;
    strncpy(init->name, "init", MAX_NAME - 1);
    init->state = PROC_READY;
    init->context.sp = (uint64_t)&kstacks[idx][KSTACK_SIZE];
    init->context.ra = (uint64_t)proc_entry;

    // Initialize file descriptors (stdin/stdout/stderr) for the init process
    fd_init(&init->fds);

    // Prepare argv for /bin/echo
    static char *argv[] = { "sh", NULL };
    // Set current to init so exec uses the right process
    current = init;
    if (exec("/bin/init", argv) < 0) {
        printk("[init] exec failed for /bin/init, falling back to /bin/sh\n");
        static char *sh_argv[] = { "sh", NULL };
        if (exec("/bin/sh", sh_argv) < 0)
            printk("[init] exec failed fallback to /bin/sh\n");
    }

    // static char *argv[] = { "forktest", NULL };
    // current = init;
    // if (exec("/bin/forktest", argv) < 0) {
    //     printk("[forktest] exec failed\n");
    // }


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

    // User-mode processes allocates a trap_frame to save/restore user registers
    // p->trapframe = (trap_frame *)kmalloc(sizeof(trap_frame));
    // memset(p->trapframe, 0, sizeof(trap_frame));

    // Initialize process' file descriptor
    fd_init(&p->fds);

    proc_ready(p);
    return p;
}

/* Free just the address space — used by exec() to drop the old image */
static void proc_free_pagetable(struct proc *p) {
    if (p->pagetable) {
        vmm_free_pagetable(p->pagetable);
        p->pagetable = NULL;
    }
}

/* Full teardown — used by proc_exit() */
void proc_destroy(struct proc *p) {
    if (!p) return;

    proc_free_pagetable(p);

    if (p->trapframe) {
        kfree(p->trapframe);
        p->trapframe = NULL;
    }

    fd_destroy(&p->fds);
}

void proc_free(struct proc *p) {
    if (!p) return;
    p->pid = 0;
    p->state = PROC_UNUSED;
    p->next = NULL;
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
                    *exit_code_out = child->exit_code;
                    clear_csr_bit(sstatus, SSTATUS_SUM);
                }

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
    file_t *f = vfs_open(path);
    if (!f) return -1;

    // 2. Read and validate the ELF64 header
    elf_header_t ehdr;
    if (vfs_read(f, &ehdr, sizeof(ehdr)) != (int)sizeof(ehdr))
        goto fail_file;
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F' ||
        ehdr.e_ident[4] != 2 /* ELFCLASS64 */) {
        goto fail_file;
    }

    // 3. Create a fresh page table for the new image BEFORE touching anything
    pagetable_t new_pt = vmm_create_pagetable();
    if (!new_pt) goto fail_file;

    // 4. Load each PT_LOAD segment into the new page table
    for (int i = 0; i < ehdr.e_phnum; i++) {
        elf_prog_header_t phdr;
        if (vfs_lseek(f, (size_t)(ehdr.e_phoff + (uint64_t)i * sizeof(phdr)), SEEK_SET) < 0)
            goto fail_pt;
        if (vfs_read(f, &phdr, sizeof(phdr)) != (int)sizeof(phdr))
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
        if (vfs_lseek(f, (size_t)phdr.p_offset, SEEK_SET) < 0)
            goto fail_pt;
        size_t remaining = (size_t)phdr.p_filesz;
        uintptr_t va     = phdr.p_vaddr;
        while (remaining > 0) {
            // Translate user VA → physical address (identity-mapped in kernel)
            uintptr_t pa = vmm_translate(new_pt, va);
            if (!pa) goto fail_pt;
            size_t chunk = PGSIZE - (va & (PGSIZE - 1));
            if (chunk > remaining) chunk = remaining;
            if (vfs_read(f, (void *)pa, chunk) != (int)chunk)
                goto fail_pt;
            va        += chunk;
            remaining -= chunk;
        }
        // p_memsz > p_filesz (BSS) is already zero from memset above
    }

    // 5. Allocate and map user stack (just below KERNBASE)
    uintptr_t stack_top  = KERNBASE;
    uintptr_t stack_base = stack_top - USTACK_SIZE;
    for (uintptr_t va = stack_base; va < stack_top; va += PGSIZE) {
        void *pg = pmm_alloc_page();
        if (!pg) goto fail_pt;
        memset(pg, 0, PGSIZE);
        if (vmm_map(new_pt, va, (uintptr_t)pg, PTE_R | PTE_W | PTE_U) < 0) {
            pmm_free_page(pg);
            goto fail_pt;
        }
    }

    // 6. Copy argv strings onto the user stack via physical addresses
    int argc = 0;
    while (argv && argv[argc]) argc++;

    uintptr_t *argv_uvas = (uintptr_t *)kmalloc((size_t)(argc + 1) * sizeof(uintptr_t));
    if (!argv_uvas) goto fail_pt;

    uintptr_t sp = stack_top;
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        uintptr_t pa = vmm_translate(new_pt, sp);
        if (!pa) { kfree(argv_uvas); goto fail_pt; }
        memcpy((void *)pa, argv[i], len);
        argv_uvas[i] = sp;   // user-space VA of this string
    }
    argv_uvas[argc] = 0;     // NULL terminator

    // Align sp to 16 bytes, then push the argv pointer array
    sp &= ~(uintptr_t)0xF;
    sp -= (uint64_t)(argc + 1) * sizeof(uintptr_t);
    for (int i = 0; i <= argc; i++) {
        uintptr_t slot_va = sp + (uint64_t)i * sizeof(uintptr_t);
        uintptr_t slot_pa = vmm_translate(new_pt, slot_va);
        if (!slot_pa) { kfree(argv_uvas); goto fail_pt; }
        *(uintptr_t *)slot_pa = argv_uvas[i];
    }
    uintptr_t user_argv = sp;
    kfree(argv_uvas);

    // 7. Commit: discard old address space, install the new one
    proc_free_pagetable(current);
    current->pagetable = new_pt;
    current->sz        = stack_top;
    current->satp      = MAKE_SATP((uintptr_t)new_pt);

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

    // Switch to the new page table
    vmm_switch(current->pagetable);

    vfs_close(f);
    return 0;

fail_pt:
    vmm_free_pagetable(new_pt);
fail_file:
    vfs_close(f);
    return -1;
}

