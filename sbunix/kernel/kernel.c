#include <printk.h>
#include <process.h>

void boot(void) {
  printk("Booting SBUnix\n");
  proc_init(); // Initialization of process table and creation of kernel/init process
  
  // Start scheduler
  struct proc *p = proc_create("init");
  if (p) context_switch(p);
  
  printk("Awaiting input.... >\n");
}