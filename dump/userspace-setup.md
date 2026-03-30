# User-Space Binaries Setup

## Overview
This document describes the user-space binaries and libc setup for SBUnix.

## Build Process

The Makefile automatically:
1. Discovers all programs in `bin/*/` directories
2. Compiles each program against libc (`build/libc.a`)
3. Links with `crt.S` (C runtime startup code)
4. Places binaries in `build/rootfs/bin/`
5. Copies static files from `rootfs/` to `build/rootfs/`
6. Creates a tar archive of the complete filesystem
7. Embeds the tar archive into the kernel image as `.tarfs` section

## User-Space Binaries Created

### Essential Shell and Utilities
- **sh**: Simple shell with built-in `cd` and `exit`, executes commands from `/bin/`
- **ls**: List directory contents
- **cat**: Concatenate and display files
- **echo**: Print arguments to stdout
- **pwd**: Print working directory
- **cp**: Copy files
- **rm**: Remove files
- **mkdir**: Create directories
- **sleep**: Sleep for specified seconds

### Process Management
- **ps**: Display process information (currently just shows PID)
- **kill**: Send signals to processes

### Testing
- **test**: Comprehensive test program that exercises fork, exec, I/O, strings, etc.

### Utilities
- **true**: Exit with success (0)
- **false**: Exit with failure (1)
- **uname**: Print system information

## libc Implementation Status

### Fully Implemented
- **Process**: fork, execve, getpid, wait, waitpid, _exit
- **I/O**: read, write, open, close, creat
- **String**: strlen, strcpy, strcat, strcmp, strchr, strrchr, strdup, memcpy, memset, memmove, memcmp
- **Memory**: malloc, calloc, realloc, free (using mmap-based allocator)
- **Stdio**: printf, fprintf, sprintf, snprintf, puts, putchar, getchar
- **Stdlib**: atoi, atol, strtol, strtoul, exit, abort, rand, srand, qsort, bsearch
- **Ctype**: isalpha, isdigit, isspace, tolower, toupper, etc.
- **Time**: time, clock_gettime, mktime, strftime, nanosleep
- **Signal**: sigaction, kill, raise, sigprocmask, sigemptyset, etc.
- **Directory**: opendir, readdir, closedir, getcwd

### Stub Implementations (return -1 or default values)
- **I/O**: lseek, pipe, dup, dup2, fcntl, unlink
- **Process**: getppid, getuid, getgid
- **Directory**: chdir
- **Environment**: getenv, setenv, unsetenv

## Header Files

All standard headers use angle brackets (`<>`) as they are system headers:
- `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<unistd.h>`
- `<sys/types.h>`, `<sys/stat.h>`, `<sys/wait.h>`, `<fcntl.h>`
- `<signal.h>`, `<dirent.h>`, `<time.h>`, `<ctype.h>`

The `-Ilibc/include` flag in CFLAGS makes these headers available to all programs.

## Testing Approach

To test if a program will link correctly:
1. Write a test program that uses the POSIX functions you need
2. The Makefile will attempt to compile it against libc
3. Any missing symbols will be reported at link time
4. Implement the missing functions in libc

The `bin/test/test.c` program exercises common functions to verify libc completeness.

## Rootfs Structure

```
/
├── bin/           (all user-space binaries)
│   ├── sh
│   ├── ls
│   ├── cat
│   ├── echo
│   ├── pwd
│   ├── cp
│   ├── rm
│   ├── mkdir
│   ├── ps
│   ├── kill
│   ├── sleep
│   ├── true
│   ├── false
│   ├── uname
│   └── test
├── etc/
│   └── rc        (init script, launches /bin/sh)
└── hello.txt     (test file)
```

## Boot Process

1. Kernel boots and mounts the embedded tarfs
2. Kernel executes `/etc/rc` as init process
3. `/etc/rc` prints a message and execs `/bin/sh`
4. User gets an interactive shell prompt
