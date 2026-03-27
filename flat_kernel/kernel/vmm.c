#include "vmm.h"
#include "pmm.h"
#include "riscv.h"
#include "printk.h"
#include "string.h"

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
    printk("VMM: Unaligned mapping va=%p pa=%p\n", va, pa);
    return -1;
  }
  
  uint64_t *pte = vmm_walk(pagetable, va, 1);
  if (pte == NULL) {
    printk("VMM: Failed to walk page table for va=%p\n", va);
    return -1;
  }
  
  if (*pte & PTE_V) {
    printk("VMM: Remapping va=%p (was pa=%p)\n", va, PTE_TO_PA(*pte));
    return -1;
  }
  
  *pte = PA_TO_PTE(pa) | perm | PTE_V;
  return 0;
}

void vmm_unmap(pagetable_t pagetable, uintptr_t va) {
  if (va % PGSIZE != 0) {
    printk("VMM: Unaligned unmap va=%p\n", va);
    return;
  }
  
  uint64_t *pte = vmm_walk(pagetable, va, 0);
  if (pte == NULL || (*pte & PTE_V) == 0) {
    printk("VMM: Unmapping non-mapped page va=%p\n", va);
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
    if (pt == NULL) return NULL;

    for (int i = 0; i < 512; i++) pt[i] = 0;

    pagetable_t kpt = vmm_kernel_pagetable();
    if (kpt) {
        for (int i = 0; i < 512; i++) {
            if (!(kpt[i] & PTE_V)) continue;

            // L2 indices 0 and 1 cover VA < KERNBASE (0x80000000), i.e. the
            // user/device range.  The kernel maps UART here (0x10000000,
            // VPN[2]=0), so kpt[0] points to a live kernel L1 table.
            //
            // We MUST NOT share that L1 pointer in user page tables.
            // vmm_walk(new_pt, user_va, alloc=1) would follow the shared L1
            // pointer and write new L0 entries directly into the kernel's own
            // L1 table.  Every other page table that later calls
            // vmm_create_pagetable would then inherit those user entries —
            // and any vmm_unmap on one page table would silently unmap the
            // same VA in every other page table that shares the L1.
            //
            // Fix: for user-range L2 entries, allocate a private copy of the
            // L1 page so that walks stay within the new page table.
            if (i < (int)(KERNBASE >> 30)) {
                if (kpt[i] & (PTE_R | PTE_W | PTE_X)) {
                    // Huge-page leaf in user range — just copy the PTE.
                    pt[i] = kpt[i];
                } else {
                    // Non-leaf: make a private copy of the kernel L1 page.
                    pagetable_t kl1 = (pagetable_t)PTE_TO_PA(kpt[i]);
                    pagetable_t pl1 = (pagetable_t)pmm_alloc_page();
                    if (!pl1) { vmm_free_pagetable(pt); return NULL; }
                    for (int j = 0; j < 512; j++) pl1[j] = kl1[j];
                    pt[i] = PA_TO_PTE((uintptr_t)pl1) | (kpt[i] & 0x3FF);
                }
            } else {
                // Kernel range (VA >= KERNBASE): safe to share — these are
                // identity-mapped RAM pages that the kernel never walks via a
                // user page table, so no user entry can be installed there.
                pt[i] = kpt[i];
            }
        }
    }

    return pt;
}

static void vmm_free_pagetable_recursive(pagetable_t pagetable, int level) {
    if (level > 0) {
        pagetable_t kpt = vmm_kernel_pagetable();

        for (int i = 0; i < 512; i++) {
            uint64_t pte = pagetable[i];
            if (!((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0))
                continue;

            // Skip entries shared with the kernel page table — they are
            // owned by the kernel and must not be freed here.
            if (kpt && pagetable[i] == kpt[i])
                continue;

            pagetable_t child = (pagetable_t)PTE_TO_PA(pte);
            vmm_free_pagetable_recursive(child, level - 1);
        }
    }
    pmm_free_page(pagetable);
}

void vmm_free_pagetable(pagetable_t pagetable) {
    if (pagetable == NULL) return;
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
      printk("VMM: Failed to map kernel page at %p\n", pa);
      return;
    }
  }
  
  // Map remaining physical memory (for PMM and dynamic allocation)
  for (uintptr_t pa = kernel_end; pa < PHYSTOP; pa += PGSIZE) {
    if (vmm_map(kernel_pagetable, pa, pa, PTE_R | PTE_W) != 0) {
      printk("VMM: Failed to map memory page at %p\n", pa);
      return;
    }
  }
  
  // Map UART and other devices (0x10000000 region)
  // QEMU virt machine has devices at 0x10000000
  for (uintptr_t pa = 0x10000000; pa < 0x10010000; pa += PGSIZE) {
    if (vmm_map(kernel_pagetable, pa, pa, PTE_R | PTE_W) != 0) {
      printk("VMM: Failed to map device page at %p\n", pa);
      return;
    }
  }
  
  printk("VMM: Kernel page table created\n");
  
  // Switch to kernel page table
  vmm_switch(kernel_pagetable);
  printk("VMM: Paging enabled\n");
}

// Recursively copy user-space mappings from parent to child pagetable
static int vmm_copy_pagetable_recursive(pagetable_t child, pagetable_t parent, int level, uintptr_t va_base) {
  for (int i = 0; i < 512; i++) {
    uint64_t pte = parent[i];
    if (!(pte & PTE_V)) continue;

    if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // This is a lower-level page table
      pagetable_t parent_next = (pagetable_t)PTE_TO_PA(pte);
      pagetable_t child_next = (pagetable_t)pmm_alloc_page();
      if (!child_next) return -1;
      for (int j = 0; j < 512; j++) child_next[j] = 0;
      // Set child PTE
      child[i] = PA_TO_PTE((uintptr_t)child_next) | PTE_V;
      // Recurse
      if (vmm_copy_pagetable_recursive(child_next, parent_next, level - 1, va_base | ((uintptr_t)i << (level * 9 + 12))) < 0)
        return -1;
    } else {
      // Leaf PTE: user mapping or kernel mapping
      uintptr_t va = va_base | ((uintptr_t)i << (level * 9 + 12));
      if (va >= KERNBASE) {
        // Kernel mapping: just copy the PTE (shared)
        child[i] = pte;
      } else {
        // User mapping: allocate new page, copy contents
        void *newpage = pmm_alloc_page();
        if (!newpage) return -1;
        void *oldpage = (void *)PTE_TO_PA(pte);
        memcpy(newpage, oldpage, PGSIZE);
        child[i] = PA_TO_PTE((uintptr_t)newpage) | (pte & (PTE_R | PTE_W | PTE_X | PTE_U | PTE_V));
      }
    }
  }
  return 0;
}

// Clone a parent's page table for fork()
pagetable_t vmm_copy_pagetable(pagetable_t parent) {
  pagetable_t child = vmm_create_pagetable();
  if (!child) return NULL;
  // Recursively copy user mappings (levels 2, 1, 0)
  if (vmm_copy_pagetable_recursive(child, parent, 2, 0) < 0) {
    vmm_free_pagetable(child);
    return NULL;
  }
  return child;
}
