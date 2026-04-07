#pragma once
#include "kernel/types.h"

typedef struct dentry {
    char d_name[256];
    struct dentry *d_parent;
    struct vnode *d_vnode;
    int d_ref;
} dentry_t;

// Lifecycle
void dentry_pool_init(void);
dentry_t *dentry_alloc(const char *name, struct vnode *vnode, dentry_t *parent);
void dentry_get(dentry_t *d); // acquire ref
void dentry_put(dentry_t *d); // release ref, free chain if 0

// Utilities
dentry_t *dentry_lookup_child(dentry_t *parent, const char *name);
int dentry_get_parent_path(const char *path, char *buf, size_t size);
int dentry_build_path(dentry_t *d, dentry_t *root, char *buf, size_t size); // for getcwd