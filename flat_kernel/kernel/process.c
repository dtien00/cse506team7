#include "process.h"
#include "kmalloc.h"
#include "printk.h"

static process_t proc_table[MAX_PROCS];
static process_t *current_proc = NULL;
static pid_t next_pid = 1;

void proc_init(void) {
  // Initialize process table
  for (int i = 0; i < MAX_PROCS; i++) {
    proc_table[i].state = PROC_UNUSED;
    proc_table[i].pid = 0;
  }
  
  printk("Process: Initialized (max %d processes)\n", MAX_PROCS);
}

process_t *proc_alloc(void) {
  // Find unused slot
  for (int i = 0; i < MAX_PROCS; i++) {
    if (proc_table[i].state == PROC_UNUSED) {
      process_t *p = &proc_table[i];
      
      // Initialize process
      p->pid = next_pid++;
      p->state = PROC_EMBRYO;
      p->pagetable = NULL;
      p->sz = 0;
      p->parent = NULL;
      p->exit_status = 0;
      p->trapframe = NULL;
      
      // Clear name
      for (int j = 0; j < PROC_NAME_LEN; j++) {
        p->name[j] = 0;
      }
      
      // Clear context
      p->context_sp = 0;
      p->context_ra = 0;
      for (int j = 0; j < 12; j++) {
        p->context_s[j] = 0;
      }
      
      return p;
    }
  }
  
  return NULL;  // No free slots
}

void proc_free(process_t *p) {
  if (p == NULL) {
    return;
  }
  
  // Free page table if allocated
  if (p->pagetable != NULL) {
    vmm_free_pagetable(p->pagetable);
    p->pagetable = NULL;
  }
  
  // Free trapframe if allocated
  if (p->trapframe != NULL) {
    kfree(p->trapframe);
    p->trapframe = NULL;
  }
  
  p->state = PROC_UNUSED;
  p->pid = 0;
}

process_t *proc_current(void) {
  return current_proc;
}

void proc_set_current(process_t *p) {
  current_proc = p;
  if (p != NULL) {
    p->state = PROC_RUNNING;
  }
}

process_t *proc_find(pid_t pid) {
  for (int i = 0; i < MAX_PROCS; i++) {
    if (proc_table[i].state != PROC_UNUSED && proc_table[i].pid == pid) {
      return &proc_table[i];
    }
  }
  return NULL;
}

void proc_dump(void) {
  printk("\nProcess table:\n");
  printk("PID  State      Name\n");
  printk("---  ---------  ----------------\n");
  
  for (int i = 0; i < MAX_PROCS; i++) {
    if (proc_table[i].state != PROC_UNUSED) {
      const char *state_str;
      switch (proc_table[i].state) {
        case PROC_EMBRYO:   state_str = "EMBRYO"; break;
        case PROC_RUNNABLE: state_str = "RUNNABLE"; break;
        case PROC_RUNNING:  state_str = "RUNNING"; break;
        case PROC_SLEEPING: state_str = "SLEEPING"; break;
        case PROC_ZOMBIE:   state_str = "ZOMBIE"; break;
        default:            state_str = "UNKNOWN"; break;
      }
      
      printk("%3d  %-9s  %s\n", proc_table[i].pid, state_str, proc_table[i].name);
    }
  }
}
