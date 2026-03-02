#include "mm/vmm.h"
#include "mm/pmm.h"
#include "arch/riscv.h"
#include "kernel/printk.h"

static pagetable_t kernel_pagetable = NULL;

// External symbols from linker script
extern char _kernel_end[];

// Walk the page table to find PTE for a virtual address
uint64_t *vmm_walk(pagetable_t pagetable, uintptr_t va, int alloc) {
  if (va >= MAXVA) {
    return NULL;
  }
  
  for (int level = 2; level > 0; level--) {
    uint64_t *pte = &pagetable[PX(level, va)];
    
    if (*pte & PTE_V) {
      // Valid entry, go to next level
      pagetable = (pagetable_t)PTE_TO_PA(*pte);
    } else {
      // Invalid entry
      if (!alloc) {
        return NULL;
      }
      
      // Allocate a new page table
      pagetable = (pagetable_t)pmm_alloc_page();
      if (pagetable == NULL) {
        return NULL;
      }
      
      // Zero out the new page table
      for (int i = 0; i < 512; i++) {
        pagetable[i] = 0;
      }
      
      // Update the PTE to point to the new page table
      *pte = PA_TO_PTE((uintptr_t)pagetable) | PTE_V;
    }
  }
  
  // Return PTE for the final level
  return &pagetable[PX(0, va)];
}

int vmm_map(pagetable_t pagetable, uintptr_t va, uintptr_t pa, uint64_t perm) {
  // Check alignment
  if (va % PGSIZE != 0 || pa % PGSIZE != 0) {
    printk("VMM: Unaligned mapping va=0x%lx pa=0x%lx\n", va, pa);
    return -1;
  }
  
  uint64_t *pte = vmm_walk(pagetable, va, 1);
  if (pte == NULL) {
    printk("VMM: Failed to walk page table for va=0x%lx\n", va);
    return -1;
  }
  
  if (*pte & PTE_V) {
    printk("VMM: Remapping va=0x%lx (was pa=0x%lx)\n", va, PTE_TO_PA(*pte));
    return -1;
  }
  
  *pte = PA_TO_PTE(pa) | perm | PTE_V;
  return 0;
}

void vmm_unmap(pagetable_t pagetable, uintptr_t va) {
  if (va % PGSIZE != 0) {
    printk("VMM: Unaligned unmap va=0x%lx\n", va);
    return;
  }
  
  uint64_t *pte = vmm_walk(pagetable, va, 0);
  if (pte == NULL || (*pte & PTE_V) == 0) {
    printk("VMM: Unmapping non-mapped page va=0x%lx\n", va);
    return;
  }
  
  *pte = 0;
}

int vmm_map_range(pagetable_t pagetable, uintptr_t va, uintptr_t pa, size_t size, uint64_t perm) {
  uintptr_t va_end = va + size;
  
  for (uintptr_t a = va, p = pa; a < va_end; a += PGSIZE, p += PGSIZE) {
    if (vmm_map(pagetable, a, p, perm) != 0) {
      // Unmap what we've mapped so far
      vmm_unmap_range(pagetable, va, a - va);
      return -1;
    }
  }
  
  return 0;
}

void vmm_unmap_range(pagetable_t pagetable, uintptr_t va, size_t size) {
  uintptr_t va_end = va + size;
  
  for (uintptr_t a = va; a < va_end; a += PGSIZE) {
    vmm_unmap(pagetable, a);
  }
}

uintptr_t vmm_translate(pagetable_t pagetable, uintptr_t va) {
  if (va >= MAXVA) {
    return 0;
  }
  
  uint64_t *pte = vmm_walk(pagetable, va, 0);
  if (pte == NULL || (*pte & PTE_V) == 0) {
    return 0;
  }
  
  return PTE_TO_PA(*pte) | (va & (PGSIZE - 1));
}

pagetable_t vmm_create_pagetable(void) {
  pagetable_t pt = (pagetable_t)pmm_alloc_page();
  if (pt == NULL) {
    return NULL;
  }
  
  // Zero out the page table
  for (int i = 0; i < 512; i++) {
    pt[i] = 0;
  }
  
  return pt;
}

// Recursively free page table and its children
static void vmm_free_pagetable_recursive(pagetable_t pagetable, int level) {
  if (level > 0) {
    // Free child page tables
    for (int i = 0; i < 512; i++) {
      uint64_t pte = pagetable[i];
      if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
        // This is a pointer to another page table
        pagetable_t child = (pagetable_t)PTE_TO_PA(pte);
        vmm_free_pagetable_recursive(child, level - 1);
      }
    }
  }
  
  // Free this page table
  pmm_free_page(pagetable);
}

void vmm_free_pagetable(pagetable_t pagetable) {
  if (pagetable == NULL) {
    return;
  }
  
  vmm_free_pagetable_recursive(pagetable, 2);
}

pagetable_t vmm_kernel_pagetable(void) {
  return kernel_pagetable;
}

void vmm_switch(pagetable_t pagetable) {
  w_satp(MAKE_SATP((uintptr_t)pagetable));
}

void vmm_init(void) {
  // Create kernel page table
  kernel_pagetable = vmm_create_pagetable();
  if (kernel_pagetable == NULL) {
    printk("VMM: Failed to create kernel page table\n");
    return;
  }
  
  // Identity map kernel memory (0x80000000 - 0x88000000)
  // This allows kernel to run with paging enabled
  printk("VMM: Mapping kernel memory...\n");
  
  uintptr_t kernel_start = KERNBASE;
  uintptr_t kernel_end = PGROUNDUP((uintptr_t)_kernel_end);
  
  // Map kernel text, data, bss
  for (uintptr_t pa = kernel_start; pa < kernel_end; pa += PGSIZE) {
    if (vmm_map(kernel_pagetable, pa, pa, PTE_R | PTE_W | PTE_X) != 0) {
      printk("VMM: Failed to map kernel page at 0x%lx\n", pa);
      return;
    }
  }
  
  // Map remaining physical memory (for PMM and dynamic allocation)
  for (uintptr_t pa = kernel_end; pa < PHYSTOP; pa += PGSIZE) {
    if (vmm_map(kernel_pagetable, pa, pa, PTE_R | PTE_W) != 0) {
      printk("VMM: Failed to map memory page at 0x%lx\n", pa);
      return;
    }
  }
  
  // Map UART and other devices (0x10000000 region)
  // QEMU virt machine has devices at 0x10000000
  for (uintptr_t pa = 0x10000000; pa < 0x10010000; pa += PGSIZE) {
    if (vmm_map(kernel_pagetable, pa, pa, PTE_R | PTE_W) != 0) {
      printk("VMM: Failed to map device page at 0x%lx\n", pa);
      return;
    }
  }
  
  printk("VMM: Kernel page table created\n");
  
  // Switch to kernel page table
  vmm_switch(kernel_pagetable);
  printk("VMM: Paging enabled\n");
}
