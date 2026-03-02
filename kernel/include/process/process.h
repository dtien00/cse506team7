#pragma once

#include "kernel/types.h"
#include "mm/vmm.h"
#include "trap/trap.h"

// Process states
typedef enum {
  PROC_UNUSED = 0,
  PROC_EMBRYO,     // Being created
  PROC_RUNNABLE,   // Ready to run
  PROC_RUNNING,    // Currently running
  PROC_SLEEPING,   // Waiting for event
  PROC_ZOMBIE      // Exited but not yet cleaned up
} proc_state_t;

// Process ID type
typedef int pid_t;

#define MAX_PROCS 64
#define PROC_NAME_LEN 16

// Process control block
typedef struct process {
  pid_t pid;                    // Process ID
  proc_state_t state;           // Process state
  char name[PROC_NAME_LEN];     // Process name
  
  // Memory management
  pagetable_t pagetable;        // Page table
  uint64_t sz;                  // Size of process memory (bytes)
  
  // Saved context for scheduling
  uint64_t context_sp;          // Saved stack pointer
  uint64_t context_ra;          // Saved return address
  uint64_t context_s[12];       // Saved registers s0-s11
  
  // Trap frame (for user mode)
  trapframe_t *trapframe;
  
  // Parent/child relationships
  struct process *parent;
  
  // Exit status
  int exit_status;
  
  // File descriptors (TODO)
  // void *ofile[MAX_FDS];
  
  // Working directory (TODO)
  // struct inode *cwd;
} process_t;

// Initialize process subsystem
void proc_init(void);

// Allocate a new process
process_t *proc_alloc(void);

// Free a process
void proc_free(process_t *p);

// Get current process
process_t *proc_current(void);

// Set current process
void proc_set_current(process_t *p);

// Find process by PID
process_t *proc_find(pid_t pid);

// Print process table
void proc_dump(void);
