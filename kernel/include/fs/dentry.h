#pragma once
#include "kernel/types.h"
#include "errno.h"

typedef enum {
    DENTRY_NONE = 0,
    DENTRY_ROOT = 1u << 0, // global root or mount root
    DENTRY_COVER = 1u << 1, // mount cover dentry
} dentry_flags_t;

typedef struct dentry {
    char d_name[256];

    struct dentry *d_parent; // parent in namespace tree
    struct dentry *d_children; // first child
    struct dentry *d_sibling; // next sibling in parent's child list

    struct vnode *d_vnode; // resolved object for this name
    int d_ref; // external refs: cwd, root, open file, temp holders
    uint32_t d_flags; // is this dentry special?
} dentry_t;

// Lifecycle
// Pool / lifecycle
void dentry_pool_init(void);
dentry_t *dentry_alloc(const char *name, struct vnode *vnode);
void dentry_free(dentry_t *d);

// Refcounting
void dentry_get(dentry_t *d);
void dentry_put(dentry_t *d);

// Tree operations
int dentry_attach(dentry_t *parent, dentry_t *child);
void dentry_detach(dentry_t *child);
dentry_t *dentry_lookup_child(dentry_t *parent, const char *name);

// Queries / maintenance
int dentry_is_root(const dentry_t *d);
int dentry_has_children(const dentry_t *d);
void dentry_prune(dentry_t *d); // free only if detached + unreferenced + leaf

// Debug / utilities
void dentry_dump(dentry_t *d);
int dentry_get_parent_path(const char *path, char *buf, size_t size);
int dentry_build_path(dentry_t *d, dentry_t *root, char *buf, size_t size);

// Path utilities
int dentry_normalize_path(struct dentry *start, struct dentry *root,
                          const char *input, char *out, size_t size);
int dentry_split_path(char *path_buf, char **comps, int max_comps);
