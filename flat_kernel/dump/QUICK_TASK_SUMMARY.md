# Quick Task Summary - SBUnix Project

## Foundation Status: ✅ COMPLETE

All base infrastructure is done. **Everyone can start working NOW in parallel.**

---

## Task Distribution

### Person 1: Scheduler & Context Switching
**Estimated:** ~400 lines | **Blocks:** None

```
Files to Create:
├── kernel/include/process/scheduler.h
├── kernel/process/scheduler.c
├── kernel/arch/riscv/context_switch.S
└── kernel/process/proc_lifecycle.c

Key Functions:
- scheduler_init()
- schedule() - pick next process
- yield() - give up CPU
- context_switch() - switch processes (assembly)
- sleep() / wakeup() - blocking

Tests:
- Create 2+ processes, verify they run
- Timer interrupts trigger scheduling
- Registers preserved across switches
```

---

### Person 2: Fork/Exec/Wait System Calls
**Estimated:** ~500 lines | **Blocks:** None (TARFS integration later)

```
Files to Create:
├── kernel/include/kernel/elf.h
├── kernel/core/elf.c
├── kernel/trap/syscall_proc.c
└── kernel/process/proc_copy.c

Key Functions:
- elf_parse() - parse ELF headers
- elf_load() - load program into memory
- sys_fork() - duplicate process
- sys_exec() - load new program
- sys_exit() - terminate process
- sys_wait() - wait for child

Tests:
- fork() creates child process
- exec() loads ELF binary
- Parent can wait() for child
- Process lifecycle works
```

---

### Person 3: File System (TARFS/VFS)
**Estimated:** ~600 lines | **Blocks:** None

```
Files to Create:
├── kernel/include/fs/vfs.h
├── kernel/fs/vfs.c
├── kernel/include/fs/tarfs.h
├── kernel/fs/tarfs.c
├── kernel/fs/file_descriptor.c
└── kernel/trap/syscall_fs.c

Key Functions:
- tarfs_init() - parse tar archive
- tarfs_lookup() - find file by path
- tarfs_read() - read file contents
- vfs_open() / vfs_close()
- fd_alloc() / fd_get() / fd_close()
- sys_open() / sys_close()

Tests:
- Can list files in tar archive
- Can read file contents
- File descriptors work
- open/close syscalls work
```

---

### Person 4: I/O & Device Drivers
**Estimated:** ~400 lines | **Blocks:** None (VFS integration later)

```
Files to Create:
├── kernel/include/drivers/uart.h
├── kernel/drivers/uart.c
├── kernel/trap/syscall_io.c
└── kernel/trap/syscall_mem.c

Key Functions:
- uart_init() - initialize UART hardware
- uart_putc() / uart_getc()
- sys_read() - read from stdin/files
- sys_write() - write to stdout/files
- sys_sbrk() - grow user heap

Tests:
- UART sends/receives characters
- Can read from stdin
- Can write to stdout/stderr
- sbrk() grows heap correctly
```

---

## Timeline

### Week 1: Independent Development
**Everyone works on their own component**

| Day | Person 1 | Person 2 | Person 3 | Person 4 |
|-----|----------|----------|----------|----------|
| Mon | Context switch | ELF parser | TAR parser | UART driver |
| Tue | Context switch | ELF loader | TARFS driver | UART testing |
| Wed | Scheduler | Fork syscall | VFS layer | sbrk syscall |
| Thu | Scheduler | Exec syscall | File descriptors | read syscall |
| Fri | Testing | Exit/Wait | open/close | write syscall |

**End of Week 1:** Everyone has working component in their branch

### Week 2: Integration
**Monday-Wednesday:** Merge branches, resolve conflicts

**Integration Points:**
- Person 2 + Person 3: exec() uses TARFS to load files
- Person 4 + Person 3: read/write use VFS for file I/O
- Person 1 + All: Scheduler works with all processes

**Thursday-Friday:** Integration testing
- Test process creation and scheduling
- Test loading and running user programs
- Test file I/O from user space

### Week 3: Testing & Polish
- Run echo, ls, cat programs
- Run shell
- Stress testing
- Bug fixes
- Performance tuning

---

## Dependency Graph

```
Foundation (COMPLETE)
├── Memory Management ✅
├── Trap Handling ✅
├── Process Structures ✅
└── Syscall Framework ✅

Week 1 (PARALLEL - No dependencies)
├── Person 1: Scheduler ⚡
├── Person 2: Fork/Exec ⚡
├── Person 3: TARFS/VFS ⚡
└── Person 4: UART/I/O ⚡

Week 2 (INTEGRATION)
├── Person 2 ──uses──> Person 3 (exec needs TARFS)
├── Person 4 ──uses──> Person 3 (I/O needs VFS)
└── All ──uses──> Person 1 (processes need scheduler)

Week 3 (TESTING)
└── Full system integration
```

---

## Why This Works

### ✅ No Blocking
- Each person has independent work for Week 1
- No one waits for anyone else
- Integration happens after components are done

### ✅ Balanced Load
- Each person: 400-600 lines of code
- Similar complexity across tasks
- Everyone stays busy

### ✅ Clear Interfaces
- All interfaces already defined
- Each component has clear inputs/outputs
- Integration is straightforward

### ✅ Testable Independently
- Person 1: Test with kernel threads
- Person 2: Test with hardcoded ELF data
- Person 3: Test by reading tar files
- Person 4: Test with console I/O

---

## Quick Start Commands

### Person 1
```bash
git checkout -b person1-scheduler
mkdir -p kernel/process kernel/arch/riscv
touch kernel/include/process/scheduler.h
touch kernel/process/scheduler.c
touch kernel/arch/riscv/context_switch.S
# Start coding!
```

### Person 2
```bash
git checkout -b person2-syscalls
mkdir -p kernel/core kernel/trap
touch kernel/include/kernel/elf.h
touch kernel/core/elf.c
touch kernel/trap/syscall_proc.c
# Start coding!
```

### Person 3
```bash
git checkout -b person3-filesystem
mkdir -p kernel/fs
touch kernel/include/fs/vfs.h kernel/include/fs/tarfs.h
touch kernel/fs/vfs.c kernel/fs/tarfs.c
touch kernel/fs/file_descriptor.c
# Start coding!
```

### Person 4
```bash
git checkout -b person4-io
mkdir -p kernel/drivers kernel/trap
touch kernel/include/drivers/uart.h
touch kernel/drivers/uart.c
touch kernel/trap/syscall_io.c kernel/trap/syscall_mem.c
# Start coding!
```

---

## Success Criteria

### End of Week 1
- [ ] Person 1: Scheduler switches between 2+ processes
- [ ] Person 2: Can parse ELF file and fork works
- [ ] Person 3: Can read files from TARFS
- [ ] Person 4: UART works, can read/write console

### End of Week 2
- [ ] All code merged into main branch
- [ ] Kernel compiles without errors
- [ ] Can create and schedule processes
- [ ] Can load and run user programs
- [ ] File I/O works

### End of Week 3
- [ ] Shell runs interactively
- [ ] Can execute multiple commands
- [ ] File operations work from user space
- [ ] System is stable and tested

---

## Need Help?

1. **Check existing code:** Look at similar patterns in foundation code
2. **Check xv6-riscv:** Reference implementation for ideas
3. **Ask team:** Use team chat or daily standup
4. **Read docs:** GETTING_STARTED.md, QUICK_REFERENCE.md, ARCHITECTURE.md

**Remember:** Everyone is learning. It's okay to ask questions!
