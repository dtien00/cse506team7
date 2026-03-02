#include "kernel/printk.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/kmalloc.h"
#include "trap/trap.h"
#include "process/process.h"
#include "trap/syscall.h"
#include "arch/riscv.h"

// External symbols from linker script
extern char _kernel_end[];

void boot(void) {
  printk("\n");
  printk("========================================\n");
  printk("       SBUnix Operating System\n");
  printk("       RISC-V 64-bit Kernel\n");
  printk("========================================\n");
  printk("\n");
  
  // Calculate kernel size
  uintptr_t kernel_start = 0x80200000;
  uintptr_t kernel_end = (uintptr_t)_kernel_end;
  size_t kernel_size = kernel_end - kernel_start;
  
  printk("Kernel: 0x%lx - 0x%lx (%lu KB)\n", 
         kernel_start, kernel_end, kernel_size / 1024);
  
  // Initialize physical memory manager
  // Start after kernel, manage up to 128MB
  uintptr_t mem_start = PGROUNDUP(kernel_end);
  size_t mem_size = 0x88000000 - mem_start;  // Up to 128MB total
  pmm_init(mem_start, mem_size);
  
  // Initialize virtual memory manager
  vmm_init();
  
  // Initialize kernel heap
  kmalloc_init();
  
  // Initialize trap handling
  trap_init();
  
  // Initialize process management
  proc_init();
  
  // Initialize system calls
  syscall_init();
  
  printk("\n");
  printk("========================================\n");
  printk("  Kernel initialization complete!\n");
  printk("========================================\n");
  printk("\n");
  
  // Test memory allocation
  printk("Testing memory allocation...\n");
  void *test1 = kmalloc(100);
  void *test2 = kmalloc(200);
  void *test3 = kmalloc(300);
  printk("  Allocated: %p, %p, %p\n", test1, test2, test3);
  kfree(test2);
  printk("  Freed: %p\n", test2);
  void *test4 = kmalloc(150);
  printk("  Allocated: %p\n", test4);
  kmalloc_stats();
  
  printk("\n");
  printk("Kernel ready. Waiting for interrupts...\n");
  
  // Enable interrupts
  intr_on();
  
  // Idle loop
  while (1) {
    asm volatile("wfi");  // Wait for interrupt
  }
}
