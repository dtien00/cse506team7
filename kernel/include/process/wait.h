#pragma once

/*
 * Kernel-side wait macros (mirrors libc/include/sys/wait.h).
 * Kept in-tree so the kernel never depends on libc headers and the
 * flat_kernel build produces a matching kwait.h automatically.
 */

/* waitpid() option flags */
#define WNOHANG    1
#define WUNTRACED  2
#define WCONTINUED 8

/* Status decoding macros — encoding matches Linux / POSIX convention:
 *   normal exit : (exit_code << 8) | 0x00
 *   signalled   : signal_number & 0x7f
 *   stopped     : (stop_signal << 8) | 0x7f
 *   continued   : 0xffff
 */
#define WEXITSTATUS(s)   (((s) >> 8) & 0xff)
#define WTERMSIG(s)      ((s) & 0x7f)
#define WSTOPSIG(s)      WEXITSTATUS(s)
#define WIFEXITED(s)     (WTERMSIG(s) == 0)
#define WIFSIGNALED(s)   (((s) & 0x7f) > 0 && ((s) & 0x7f) < 0x7f)
#define WIFSTOPPED(s)    (((s) & 0xff) == 0x7f)
#define WIFCONTINUED(s)  ((s) == 0xffff)
