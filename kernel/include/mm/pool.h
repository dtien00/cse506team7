#ifndef MM_POOL_H
#define MM_POOL_H

#include "kernel/types.h"

typedef struct pool_node {
    struct pool_node *next;
} pool_node_t;

typedef struct {
    pool_node_t *free_list;
    size_t       obj_size;
    size_t       capacity;
    size_t       used;
    const char  *name;
} pool_t;

void  pool_init(pool_t *p, size_t obj_size, size_t count, const char *name);
void *pool_alloc(pool_t *p);
void  pool_free(pool_t *p, void *obj);
void  pool_stats(pool_t *p);

/* ---------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------- */

// Declare a pool variable (put in your .c file at file scope)
#define POOL_DECLARE(varname) \
static pool_t varname

// Initialize — call inside your subsystem's _init() function
// Uses stringified name for printk diagnostics
#define POOL_INIT(pool, type, count) \
pool_init(&(pool), sizeof(type), (count), #pool)

// Type-safe alloc — returns (type *) or NULL
#define POOL_ALLOC(pool, type) \
((type *)pool_alloc(&(pool)))

// Free
#define POOL_FREE(pool, ptr) \
pool_free(&(pool), (void *)(ptr))

#endif