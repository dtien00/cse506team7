#include "stdlib.h"
#include "sys/mman.h"
#include "string.h"

/* ── alignment ──────────────────────────────────────────────────────────── */

#define ALIGN8(n)  (((n) + 7) & ~(size_t)7)

typedef struct block {
    size_t         size;      /* usable bytes (aligned) */
    struct block  *next;
    int            free;
    int            is_large;
} block_t;

#define BLOCK_SIZE       ALIGN8(sizeof(block_t))
#define MMAP_THRESHOLD   4096
#define ARENA_SIZE       (64 * 1024)

static block_t *head         = NULL;
static void    *arena_start  = NULL;
static void    *arena_end    = NULL;
static void    *arena_current = NULL;


#define SYS_write 4
static void do_write(const char *buf, int len) {
    if (len <= 0) return;
    register long a0 asm("a0") = 1;           /* fd = stdout */
    register long a1 asm("a1") = (long)buf;
    register long a2 asm("a2") = len;
    register long a7 asm("a7") = SYS_write;
    asm volatile("ecall" :: "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
}


/* ── internal helpers ───────────────────────────────────────────────────── */

/* Walk the free list looking for a reusable small block >= size.
   Sets *last to the last node visited (used by request_space to append). */
static block_t *find_free_block(block_t **last, size_t size) {
    block_t *cur = head;
    while (cur && !(cur->free && cur->size >= size && !cur->is_large)) {
        *last = cur;
        cur   = cur->next;
    }
    return cur;
}

/* Split block `b` so that `b` holds exactly `size` bytes and the remainder
   becomes a new free block (only if the leftover is large enough to be
   useful — at least BLOCK_SIZE + 8 bytes of payload). */
static void split_block(block_t *b, size_t size) {
    if (b->size < size + BLOCK_SIZE + 8)
        return; /* not worth splitting */

    block_t *tail = (block_t *)((char *)b + BLOCK_SIZE + size);
    tail->size    = b->size - size - BLOCK_SIZE;
    tail->next    = b->next;
    tail->free    = 1;
    tail->is_large = 0;

    b->size = size;
    b->next = tail;
}

/* Allocate a new block from the OS.
   Large requests (>= MMAP_THRESHOLD) get their own mmap region and are
   never placed on the free list — they are munmap'd directly on free().
   Small requests come from a bump-pointer arena; a new arena is mmap'd
   when the current one is exhausted. */
static block_t *request_space(block_t *last, size_t size) {
    block_t *block;

    if (size >= MMAP_THRESHOLD) {
        /* Round up to a whole number of pages. */
        size_t map_size = ALIGN8(size + BLOCK_SIZE);
        map_size = (map_size + 4095) & ~(size_t)4095;

        block = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block == MAP_FAILED)
            return NULL;

        block->size     = size;
        block->next     = NULL;   /* large blocks are NOT on the free list */
        block->free     = 0;
        block->is_large = 1;
        /* Do NOT link into the list — free() munmaps these directly and
           find_free_block() skips is_large blocks anyway. */
        return block;
    }

    /* Small path: bump from current arena, growing if needed. */
    size_t needed = size + BLOCK_SIZE;
    if (arena_start == NULL ||
        (char *)arena_current + needed > (char *)arena_end) {

        if (needed > ARENA_SIZE) {
            /* Edge case: caller asked for something that fits under
               MMAP_THRESHOLD but is larger than an arena — promote it. */
            size_t map_size = (needed + 4095) & ~(size_t)4095;
            block = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (block == MAP_FAILED)
                return NULL;
            block->size     = size;
            block->next     = NULL;
            block->free     = 0;
            block->is_large = 1;
            return block;
        }

        void *new_arena = mmap(NULL, ARENA_SIZE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (new_arena == MAP_FAILED)
            return NULL;

        arena_start   = new_arena;
        arena_end     = (char *)new_arena + ARENA_SIZE;
        arena_current = new_arena;
    }

    block             = (block_t *)arena_current;
    arena_current     = (char *)arena_current + needed;

    block->size     = size;
    block->next     = NULL;
    block->free     = 0;
    block->is_large = 0;

    if (last)
        last->next = block;

    return block;
}

/* ── public API ─────────────────────────────────────────────────────────── */

void *malloc(size_t size) {
    if (size == 0){
        // do_write("0", 1);
        return NULL;
    }

    size = ALIGN8(size);   /* always work with aligned sizes */

    block_t *block;

    if (!head) {
        block = request_space(NULL, size);
        if (!block){
            // do_write("1", 1);
            return NULL;
        }
        /* Large blocks are not on the list; only link small ones. */
        if (!block->is_large)
            head = block;
    } else {
        block_t *last = head;
        block = find_free_block(&last, size);
        if (!block) {
            block = request_space(last, size);
            if (!block){
                // do_write("2", 1);
                return NULL;
            }
            /* request_space already linked small blocks via last->next. */
        } else {
            split_block(block, size);
            block->free = 0;
        }
    }

    return (char *)block + BLOCK_SIZE;
}

void free(void *ptr) {
    if (!ptr)
        return;

    block_t *block = (block_t *)((char *)ptr - BLOCK_SIZE);

    if (block->is_large) {
        /* Remove from the list before unmapping (large blocks are not on the
           list, but guard against future refactors that might add them). */
        size_t map_size = ALIGN8(block->size + BLOCK_SIZE);
        map_size = (map_size + 4095) & ~(size_t)4095;
        munmap(block, map_size);
    } else {
        block->free = 1;
        /* Coalesce with the immediately following block if it is also free. */
        if (block->next && block->next->free && !block->next->is_large) {
            block->size += BLOCK_SIZE + block->next->size;
            block->next  = block->next->next;
        }
    }
}

void *calloc(size_t nmemb, size_t size) {
    /* Guard against nmemb * size overflow. */
    if (nmemb && size > (size_t)-1 / nmemb)
        return NULL;

    size_t total = nmemb * size;
    void  *ptr   = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr)
        return malloc(size);

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    size = ALIGN8(size);

    block_t *block = (block_t *)((char *)ptr - BLOCK_SIZE);
    if (block->size >= size)
        return ptr;   /* already large enough — no copy needed */

    void *new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}