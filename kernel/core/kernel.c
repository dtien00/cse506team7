#include "kernel/printk.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/kmalloc.h"
#include "trap/trap.h"
#include "process/process.h"
#include "trap/syscall.h"
#include "arch/riscv.h"
#include "fs/vfs.h"
#include "fs/tarfs.h"
#include "fs/tmpfs.h"
#include "fs/procfs.h"
#include "fs/ext2.h"
#include "fs/block_cache.h"
#include "drivers/virtio.h"
#include "kernel/types.h"
#include "drivers/drivers.h"
#include "kernel/console.h"

#include "tests/test.h"
#include "tty/term.h"

// External symbols from linker script
extern char _kernel_end[];

static void boot_park_forever(void) {
    while (1)
        asm volatile("wfi");
}

static void boot_fallback_to_idle(void) {
    interrupts_on();
    schedule();
    boot_park_forever();
}

static void boot_wait_for_first_tick(void) {
    while (timer_ticks == 0)
        asm volatile("wfi");
}

void run_all_tests(void) {
    // ext2_populate_test_run();
    mem_test_run();
    // trap_test_run();
    fs_test_run();
    procfs_test_run();
    sched_test_run();
    sched_timeslice_test_run();
    sched_priority_test_run();
    sched_fairness_test_run();
    sched_idle_test_run();
    sched_nice_test_run();
    proc_test_run();
    proc_waitpid_test_run();
    procinfo_test_run();
    libc_syscall_test_run();
    timer_test_run();
    kill_test_run();
    pgrp_test_run();
    sigaction_test_run();
    sigpending_test_run();
    cow_test_run();
    stack_growth_test_run();
    accounting_test_run();
    rtc_test_run();
    jobctl_test_run();
    sig_flag_test_run();
    leak_test_run();
    mm_vma_test_run();

    // kstack_overflow_test_run();   // DESTRUCTIVE: must be last — triggers kernel stack overflow panic

    // printk("%d passed, %d failed\n", passed, failed);
    test_summary_report();
}

void boot(uint64_t hart_id, uint64_t dtb) {

    // initialize console
    console_init(); // this will fall back to using the sbi driver by default
    printk("console: using %s\n", console_active_name());

    printk("\n");
    printk("========================================\n");
    printk("       SBUnix Operating System\n");
    printk("       RISC-V 64-bit Kernel\n");
    printk("========================================\n");
    printk("\n");

    uintptr_t kernel_start = 0x80200000;
    uintptr_t kernel_end = (uintptr_t)_kernel_end;
    size_t kernel_size = kernel_end - kernel_start;

    printk("Kernel: %p - %p (%d KB)\n",
           kernel_start, kernel_end, kernel_size / 1024);

    // Initialize physical memory manager
    // Start after kernel, manage up to 128MB
    uintptr_t mem_start = PGROUNDUP(kernel_end);
    size_t mem_size = 0x88000000 - mem_start;  // Up to 128MB total

    pmm_init(mem_start, mem_size);

    // Initialize trap handling
    trap_init();

    // Initialize virtual memory manager
    vmm_init();

    // Initialize kernel heap
    kmalloc_init();

    // initialize scheduler with swapper or idle loop process
    scheduler_init();

    // initialize drivers once memory is mapped
    drivers_init();

    //initialize the terminal
    term_global_init();

    // switch to uart once drivers are initialized
    if (console_set_driver("uart") == 0)
        printk("console: switched to uart\n");

    // VFS and filesystems must be ready before proc_init because
    // proc_init calls exec(), which calls vfs_open().
    // Initialize VFS and filesystems
    vfs_init();
    tarfs_init();
    tmpfs_init();
    procfs_init();

    // Initialize block storage and mount ext2 as root filesystem.
    // Discovers virtio-blk via PCIe ECAM enumeration (vendor 0x1AF4, device 0x1042)
    if (virtio_blk_init() < 0) {
        printk("FATAL: virtio block device init failed, cannot mount ext2 as root\n");
        boot_fallback_to_idle();
    }
    block_cache_init();
    ext2_init();

    if (vfs_mount(NULL, "/", "tarfs") < 0) {
        printk("WARNING: failed to mount tarfs at '/initrd'\n");
    }

    // Mount tmpfs at /tmp
    if (vfs_mount(NULL, "/tmp", "tmpfs") < 0) {
        printk("FATAL: failed to mount tmpfs at '/tmp'\n");
        boot_fallback_to_idle();
    }

    if (vfs_mount(NULL, "/mnt", "ext2") < 0) {
        printk("FATAL: failed to mount ext2 at '/'\n");
        boot_fallback_to_idle();
    }

    if (vfs_mount(NULL, "/proc", "procfs") < 0) {
        printk("WARNING: failed to mount procfs at '/proc'\n");
    }

    // root does not need to be writeable.
    // // On first boot (superblock flag == 0), copy the read-only tarfs image
    // // into the writable ext2 root so the rest of the system sees a full tree.
    // mount_t *ext2_mnt = vfs_find_mount("/");
    // tarfs_populate_ext2(ext2_mnt);
    // block_cache_flush(); // flush dirs/files written directly by populate (bypasses file_write)

    syscall_init();

    // proc_init sets up the init process and loads its ELF via exec().
    proc_init();

    interrupts_on();
    boot_wait_for_first_tick();
    printk("Turned on global interrupts. Now waiting for interrupts...\n");

    printk("\n");
    printk("========================================\n");
    printk("  Kernel initialization complete!\n");
    printk("========================================\n");
    printk("\n");

    #ifdef RUN_TESTS
        run_all_tests();
    #else
        // Hand off to the scheduler; runs the init process (pid 1).
        schedule();
        printk("Kernel panic.\n"); // should never reach here as schedule will always default to its own idle loop
        boot_park_forever();
    #endif
}
