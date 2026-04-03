#include "mm/vma.h"
#include "mm/kmalloc.h"
#include "kernel/printk.h"
#include "lib/kstring.h"

void vma_init(struct vm_area **vma_list) {
    if (vma_list) {
        *vma_list = NULL;
    }
}

struct vm_area *vma_find(struct vm_area *vma_list, uintptr_t addr) {
    struct vm_area *vma = vma_list;
    while (vma) {
        if (addr >= vma->start && addr < vma->end) {
            return vma;
        }
        vma = vma->next;
    }
    return NULL;
}

struct vm_area *vma_find_overlap(struct vm_area *vma_list, uintptr_t start, uintptr_t end) {
    struct vm_area *vma = vma_list;
    while (vma) {
        // Check if [start, end) overlaps with [vma->start, vma->end)
        if (start < vma->end && end > vma->start) {
            return vma;
        }
        vma = vma->next;
    }
    return NULL;
}

int vma_insert(struct vm_area **vma_list, struct vm_area *new_vma) {
    if (!vma_list || !new_vma) {
        return -1;
    }

    // Check for overlaps
    if (vma_find_overlap(*vma_list, new_vma->start, new_vma->end)) {
        return -1;
    }

    // Insert in sorted order by start address
    //  helps allow finding free regions easier. Otherwise does nothing.
    struct vm_area **prev_next = vma_list;
    struct vm_area *curr = *vma_list;

    while (curr && curr->start < new_vma->start) {
        prev_next = &curr->next;
        curr = curr->next;
    }

    new_vma->next = curr;
    *prev_next = new_vma;

    return 0;
}

void vma_remove(struct vm_area **vma_list, struct vm_area *vma) {
    if (!vma_list || !vma) {
        return;
    }

    struct vm_area **prev_next = vma_list;
    struct vm_area *curr = *vma_list;

    while (curr) {
        if (curr == vma) {
            *prev_next = curr->next;
            return;
        }
        prev_next = &curr->next;
        curr = curr->next;
    }
}

struct vm_area *vma_create(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags, uint32_t type) {
    if (start >= end) {
        return NULL;
    }

    struct vm_area *vma = (struct vm_area *)kmalloc(sizeof(struct vm_area));
    if (!vma) {
        return NULL;
    }

    vma->start = start;
    vma->end = end;
    vma->prot = prot;
    vma->flags = flags;
    vma->type = type;
    vma->file = NULL;
    vma->offset = 0;
    vma->next = NULL;

    return vma;
}

void vma_free(struct vm_area *vma) {
    if (vma) {
        kfree(vma);
    }
}

void vma_free_all(struct vm_area *vma_list) {
    struct vm_area *vma = vma_list;
    while (vma) {
        struct vm_area *next = vma->next;
        vma_free(vma);
        vma = next;
    }
}

uintptr_t vma_find_free_region(struct vm_area *vma_list, size_t size, uintptr_t hint, uintptr_t min_addr, uintptr_t max_addr) {
    if (size == 0 || min_addr >= max_addr) {
        return 0;
    }

    // Start searching from hint or min_addr
    uintptr_t search_start = (hint >= min_addr && hint < max_addr) ? hint : min_addr;

    // If no VMAs, return search_start if there's enough space
    if (!vma_list) {
        if (search_start + size <= max_addr) {
            return search_start;
        }
        return 0;
    }

    // Try to find a gap between VMAs
    uintptr_t candidate = search_start;
    struct vm_area *vma = vma_list;

    while (vma) {
        // Check if we can fit before this VMA
        if (candidate + size <= vma->start) {
            return candidate;
        }

        // Move candidate to after this VMA
        if (vma->end > candidate) {
            candidate = vma->end;
        }

        vma = vma->next;
    }

    // Check if we can fit after the last VMA
    if (candidate + size <= max_addr) {
        return candidate;
    }

    return 0;
}

int vma_split(struct vm_area **vma_list, struct vm_area *vma, uintptr_t addr) {
    if (!vma_list || !vma || addr <= vma->start || addr >= vma->end) {
        return -1;
    }

    // Create new VMA for the second half
    struct vm_area *new_vma = vma_create(addr, vma->end, vma->prot, vma->flags, vma->type);
    if (!new_vma) {
        return -1;
    }

    // Copy file-backed info if present
    new_vma->file = vma->file;
    new_vma->offset = vma->offset + (addr - vma->start);

    // Adjust original VMA to cover first half
    vma->end = addr;

    // Insert new VMA after the original
    new_vma->next = vma->next;
    vma->next = new_vma;

    return 0;
}

void vma_merge(struct vm_area **vma_list) {
    if (!vma_list || !*vma_list) {
        return;
    }

    struct vm_area *vma = *vma_list;

    while (vma && vma->next) {
        struct vm_area *next = vma->next;

        // Check if VMAs are adjacent and have compatible properties
        if (vma->end == next->start &&
            vma->prot == next->prot &&
            vma->flags == next->flags &&
            vma->type == next->type &&
            vma->file == next->file) {

            // Merge: extend current VMA and remove next
            vma->end = next->end;
            vma->next = next->next;
            vma_free(next);

            // Don't advance vma, check if we can merge with the new next
            continue;
        }

        vma = vma->next;
    }
}

int vma_copy_all(struct vm_area **dest_list, struct vm_area *src_list) {
    if (!dest_list) {
        return -1;
    }

    *dest_list = NULL;

    struct vm_area *src = src_list;
    struct vm_area **dest_tail = dest_list;

    while (src) {
        struct vm_area *new_vma = vma_create(src->start, src->end, src->prot, src->flags, src->type);
        if (!new_vma) {
            vma_free_all(*dest_list);
            *dest_list = NULL;
            return -1;
        }

        new_vma->file = src->file;
        new_vma->offset = src->offset;

        *dest_tail = new_vma;
        dest_tail = &new_vma->next;

        src = src->next;
    }

    return 0;
}

void vma_dump(struct vm_area *vma_list) {
    printk("VMA list:\n");

    if (!vma_list) {
        printk("  (empty)\n");
        return;
    }

    struct vm_area *vma = vma_list;
    int count = 0;

    while (vma) {
        printk("  [%d] 0x%p - 0x%p ", count++, vma->start, vma->end);

        // Print protection
        printk("prot=");
        if (vma->prot & VMA_PROT_READ) printk("r");
        else printk("-");
        if (vma->prot & VMA_PROT_WRITE) printk("w");
        else printk("-");
        if (vma->prot & VMA_PROT_EXEC) printk("x");
        else printk("-");

        // Print type
        printk(" type=");
        if (vma->type & VMA_TYPE_ANON) printk("ANON");
        else if (vma->type & VMA_TYPE_FILE) printk("FILE");
        else if (vma->type & VMA_TYPE_STACK) printk("STACK");
        else if (vma->type & VMA_TYPE_HEAP) printk("HEAP");
        else printk("UNKNOWN");

        // Print flags
        if (vma->flags & VMA_SHARED) printk(" SHARED");
        if (vma->flags & VMA_PRIVATE) printk(" PRIVATE");
        if (vma->flags & VMA_FIXED) printk(" FIXED");
        if (vma->flags & VMA_GROWSDOWN) printk(" GROWSDOWN");

        printk("\n");

        vma = vma->next;
    }
}