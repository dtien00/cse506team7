#include "mm/pool.h"
#include "mm/pmm.h"
#include "arch/riscv.h"
#include "kernel/printk.h"

// obj_size must be at least pointer-sized for the free list node to fit
#define POOL_ALIGN(s) (((s) + 7) & ~7)

void pool_init(pool_t *p, size_t obj_size, size_t count, const char *name) {
    p->name      = name;
    p->used      = 0;
    p->free_list = NULL;

    // Each slot must fit at least a pool_node_t (one pointer)
    if (obj_size < sizeof(pool_node_t))
        obj_size = sizeof(pool_node_t);
    obj_size = POOL_ALIGN(obj_size);

    size_t pages = (obj_size * count + PGSIZE - 1) / PGSIZE;
    void  *mem   = pmm_alloc_pages(pages);

    if (!mem) {
        printk("pool: FATAL — '%s' could not allocate backing pages\n", name);
        p->obj_size = obj_size;
        p->capacity = 0;
        return;
    }

    p->obj_size = obj_size;
    p->capacity = count;

    // Build singly-linked free list (stack order, O(1) push/pop)
    char *ptr = (char *)mem;
    for (size_t i = 0; i < count; i++) {
        pool_node_t *node = (pool_node_t *)ptr;
        node->next   = p->free_list;
        p->free_list = node;
        ptr += obj_size;
    }

    printk("pool: '%s' ready — %d × %d B (%d pages)\n",
           name, (int)count, (int)obj_size, (int)pages);
}

void *pool_alloc(pool_t *p) {
    if (!p->free_list) {
        printk("pool: '%s' exhausted (used=%d cap=%d)\n",
               p->name, (int)p->used, (int)p->capacity);
        return NULL;
    }
    pool_node_t *node = p->free_list;
    p->free_list = node->next;
    p->used++;
    return (void *)node;
}

void pool_free(pool_t *p, void *obj) {
    if (!obj) return;
    pool_node_t *node = (pool_node_t *)obj;
    node->next   = p->free_list;
    p->free_list = node;
    p->used--;
}

void pool_stats(pool_t *p) {
    printk("pool '%s': %d/%d used\n",
           p->name, (int)p->used, (int)p->capacity);
}