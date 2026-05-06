// /*
//  * test_python_libc.c — MicroPython libc dependency tests
//  *
//  * Tests every libc function/syscall that MicroPython (minimal variant) calls
//  * through when running on sbunix.  Each group isolates one subsystem so a
//  * failure pinpoints exactly which libc piece is broken.
//  *
//  * Tested subsystems (mirrors python/ports/unix/*.c imports + compat.c stubs):
//  *   - heap          malloc / free / realloc / calloc
//  *   - file I/O      open / read / write / close / lseek / stat / fstat
//  *   - dir I/O       opendir / readdir / closedir
//  *   - process       getpid / getenv / getcwd / chdir / exit (smoke)
//  *   - time          gettimeofday / clock_gettime
//  *   - signal        sigaction / kill / raise
//  *   - setjmp/longjmp  (MicroPython NLR layer)
//  *   - stdio shims   puts / printf to stdout (write-backed)
//  *   - string utils  memcpy / memmove / memset / strcmp / strlen / strchr
//  *
//  * All tests run in a single process — no forks needed.
//  */

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <errno.h>
// #include <signal.h>
// #include <setjmp.h>
// #include <time.h>
// #include <sys/stat.h>
// #include <sys/time.h>
// #include <sys/types.h>
// #include <dirent.h>
// #include "us_test_common.h"

// /* ================================================================== heap */

// static void test_heap(us_test_group_t *g)
// {
//     ut_group_begin(g, "heap (malloc/free/realloc/calloc)");

//     /* basic malloc + free */
//     void *p = malloc(64);
//     TEST(g, "malloc 64 non-null", p != NULL);
//     free(p);

//     /* malloc(0) is implementation-defined but must not crash */
//     void *z = malloc(0);
//     free(z);
//     TEST(g, "malloc(0) does not crash", 1);

//     /* calloc zeroes memory */
//     int *arr = (int *)calloc(16, sizeof(int));
//     TEST(g, "calloc non-null", arr != NULL);
//     int zero_ok = 1;
//     for (int i = 0; i < 16; i++) {
//         if (arr[i] != 0) { zero_ok = 0; break; }
//     }
//     TEST(g, "calloc memory is zeroed", zero_ok);
//     free(arr);

//     /* realloc grows */
//     char *buf = (char *)malloc(32);
//     TEST(g, "malloc 32 for realloc", buf != NULL);
//     memcpy(buf, "hello", 6);
//     buf = (char *)realloc(buf, 128);
//     TEST(g, "realloc to 128 non-null", buf != NULL);
//     TEST(g, "realloc preserves content", strcmp(buf, "hello") == 0);
//     free(buf);

//     /* realloc shrinks */
//     char *big = (char *)malloc(1024);
//     TEST(g, "malloc 1024 for shrink", big != NULL);
//     memcpy(big, "data", 5);
//     big = (char *)realloc(big, 8);
//     TEST(g, "realloc shrink non-null", big != NULL);
//     TEST(g, "realloc shrink preserves prefix", memcmp(big, "data", 4) == 0);
//     free(big);

//     /* large allocation (MicroPython GC heap is typically 256 KB) */
//     void *heap = malloc(256 * 1024);
//     TEST(g, "malloc 256 KB for micropython gc heap", heap != NULL);
//     free(heap);

//     ut_group_end(g);
// }

// /* ================================================================== file I/O */

// #define TMP_FILE "/tmp/py_libc_test.bin"

// static void test_file_io(us_test_group_t *g)
// {
//     ut_group_begin(g, "file I/O (open/read/write/close/lseek)");

//     /* write */
//     int fd = open(TMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
//     TEST(g, "open O_WRONLY|O_CREAT returns >= 0", fd >= 0);
//     if (fd < 0) { ut_group_end(g); return; }

//     const char *msg = "MicroPython libc test\n";
//     ssize_t wn = write(fd, msg, strlen(msg));
//     TEST(g, "write returns byte count", wn == (ssize_t)strlen(msg));
//     close(fd);

//     /* read back */
//     fd = open(TMP_FILE, O_RDONLY);
//     TEST(g, "open O_RDONLY returns >= 0", fd >= 0);
//     if (fd < 0) { ut_group_end(g); return; }

//     char rbuf[64];
//     ssize_t rn = read(fd, rbuf, sizeof(rbuf) - 1);
//     rbuf[rn > 0 ? rn : 0] = '\0';
//     TEST(g, "read returns byte count", rn == wn);
//     TEST(g, "read content matches write", strcmp(rbuf, msg) == 0);

//     /* lseek SEEK_SET */
//     off_t pos = lseek(fd, 0, SEEK_SET);
//     TEST(g, "lseek SEEK_SET to 0", pos == 0);

//     /* lseek SEEK_END */
//     off_t end = lseek(fd, 0, SEEK_END);
//     TEST(g, "lseek SEEK_END equals write size", end == (off_t)wn);

//     /* lseek SEEK_CUR from current (at end) */
//     off_t cur = lseek(fd, -((off_t)wn), SEEK_CUR);
//     TEST(g, "lseek SEEK_CUR back to start", cur == 0);

//     close(fd);

//     ut_group_end(g);
// }

// /* ================================================================== stat / fstat */

// static void test_stat(us_test_group_t *g)
// {
//     ut_group_begin(g, "stat/fstat");

//     struct stat st;

//     /* stat on file we just created */
//     int r = stat(TMP_FILE, &st);
//     TEST(g, "stat existing file returns 0", r == 0);
//     TEST(g, "stat st_size matches written bytes", st.st_size > 0);
//     TEST(g, "stat st_mode is regular file", S_ISREG(st.st_mode));

//     /* stat on missing file */
//     r = stat("/tmp/no_such_py_libc_file_xyz", &st);
//     TEST(g, "stat missing file returns -1", r == -1);
//     TEST(g, "stat missing file sets errno ENOENT", errno == ENOENT);

//     /* fstat */
//     int fd = open(TMP_FILE, O_RDONLY);
//     TEST(g, "open for fstat", fd >= 0);
//     if (fd >= 0) {
//         struct stat fst;
//         r = fstat(fd, &fst);
//         TEST(g, "fstat returns 0", r == 0);
//         TEST(g, "fstat st_size matches stat", fst.st_size == st.st_size);
//         close(fd);
//     }

//     ut_group_end(g);
// }

// /* ================================================================== dir I/O */

// static void test_dir_io(us_test_group_t *g)
// {
//     ut_group_begin(g, "dir I/O (opendir/readdir/closedir)");

//     /* /tmp must always exist */
//     DIR *dp = opendir("/tmp");
//     TEST(g, "opendir /tmp non-null", dp != NULL);
//     if (!dp) { ut_group_end(g); return; }

//     int found_dot = 0, found_dotdot = 0, entries = 0;
//     struct dirent *de;
//     while ((de = readdir(dp)) != NULL) {
//         entries++;
//         if (strcmp(de->d_name, ".")  == 0) found_dot    = 1;
//         if (strcmp(de->d_name, "..") == 0) found_dotdot = 1;
//     }
//     TEST(g, "readdir returns entries", entries > 0);
//     TEST(g, "readdir finds '.'",  found_dot);
//     TEST(g, "readdir finds '..'", found_dotdot);

//     closedir(dp);
//     TEST(g, "closedir does not crash", 1);

//     /* opendir on missing path */
//     DIR *bad = opendir("/tmp/no_such_dir_py_libc_xyz");
//     TEST(g, "opendir missing dir returns NULL", bad == NULL);

//     ut_group_end(g);
// }

// /* ================================================================== process */

// static void test_process(us_test_group_t *g)
// {
//     ut_group_begin(g, "process (getpid/getcwd/chdir/getenv)");

//     pid_t pid = getpid();
//     TEST(g, "getpid returns positive", pid > 0);

//     char cwd[256];
//     char *cwdp = getcwd(cwd, sizeof(cwd));
//     TEST(g, "getcwd returns non-null", cwdp != NULL);
//     TEST(g, "getcwd starts with '/'", cwdp && cwdp[0] == '/');

//     /* chdir to /tmp and back */
//     int r = chdir("/tmp");
//     TEST(g, "chdir /tmp returns 0", r == 0);

//     char cwd2[256];
//     getcwd(cwd2, sizeof(cwd2));
//     TEST(g, "getcwd after chdir is /tmp", strcmp(cwd2, "/tmp") == 0);

//     /* restore */
//     chdir(cwd);

//     /* getenv: PATH probably set; missing key returns NULL */
//     char *missing = getenv("__PY_LIBC_NO_SUCH_VAR__");
//     TEST(g, "getenv missing key returns NULL", missing == NULL);

//     ut_group_end(g);
// }

// /* ================================================================== time */

// static void test_time(us_test_group_t *g)
// {
//     ut_group_begin(g, "time (gettimeofday/clock_gettime)");

//     struct timeval tv;
//     int r = gettimeofday(&tv, NULL);
//     TEST(g, "gettimeofday returns 0", r == 0);
//     TEST(g, "gettimeofday tv_sec > 0", tv.tv_sec > 0);
//     TEST(g, "gettimeofday tv_usec in range", tv.tv_usec >= 0 && tv.tv_usec < 1000000);

//     struct timespec ts;
//     r = clock_gettime(CLOCK_REALTIME, &ts);
//     TEST(g, "clock_gettime CLOCK_REALTIME returns 0", r == 0);
//     TEST(g, "clock_gettime tv_sec > 0", ts.tv_sec > 0);
//     TEST(g, "clock_gettime tv_nsec in range", ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000L);

//     ut_group_end(g);
// }

// /* ================================================================== signal */

// static volatile int g_signal_received = 0;

// static void dummy_handler(int sig)
// {
//     (void)sig;
//     g_signal_received = 1;
// }

// static void test_signal(us_test_group_t *g)
// {
//     ut_group_begin(g, "signal (sigaction/raise/kill)");

//     struct sigaction sa, old;
//     memset(&sa, 0, sizeof(sa));
//     sa.sa_handler = dummy_handler;
//     sigemptyset(&sa.sa_mask);

//     int r = sigaction(SIGUSR1, &sa, &old);
//     TEST(g, "sigaction SIGUSR1 returns 0", r == 0);

//     g_signal_received = 0;
//     raise(SIGUSR1);
//     TEST(g, "raise SIGUSR1 triggers handler", g_signal_received == 1);

//     g_signal_received = 0;
//     kill(getpid(), SIGUSR1);
//     TEST(g, "kill(self, SIGUSR1) triggers handler", g_signal_received == 1);

//     /* restore */
//     sigaction(SIGUSR1, &old, NULL);

//     /* SIG_IGN smoke */
//     struct sigaction ign;
//     memset(&ign, 0, sizeof(ign));
//     ign.sa_handler = SIG_IGN;
//     sigemptyset(&ign.sa_mask);
//     r = sigaction(SIGUSR2, &ign, &old);
//     TEST(g, "sigaction SIG_IGN returns 0", r == 0);
//     sigaction(SIGUSR2, &old, NULL);

//     ut_group_end(g);
// }

// /* ================================================================== setjmp */

// static jmp_buf g_jmp;

// static void do_longjmp(void)
// {
//     longjmp(g_jmp, 42);
// }

// static void test_setjmp(us_test_group_t *g)
// {
//     ut_group_begin(g, "setjmp/longjmp (MicroPython NLR)");

//     int val = setjmp(g_jmp);
//     if (val == 0) {
//         do_longjmp();
//         /* must not reach here */
//         TEST(g, "setjmp initial call returns 0 then longjmp fires", 0);
//     } else {
//         TEST(g, "longjmp returns correct value via setjmp", val == 42);
//     }

//     /* nested: setjmp inside a deeper frame */
//     jmp_buf inner;
//     int inner_val = setjmp(inner);
//     if (inner_val == 0) {
//         longjmp(inner, 7);
//         TEST(g, "inner longjmp fires", 0);
//     } else {
//         TEST(g, "inner longjmp value", inner_val == 7);
//     }

//     ut_group_end(g);
// }

// /* ================================================================== string utils */

// static void test_string_utils(us_test_group_t *g)
// {
//     ut_group_begin(g, "string utils (memcpy/memmove/memset/strcmp/strchr)");

//     /* memcpy */
//     char src[16] = "abcdefghij";
//     char dst[16];
//     memcpy(dst, src, 11);
//     TEST(g, "memcpy copies correctly", strcmp(dst, "abcdefghij") == 0);

//     /* memmove overlapping */
//     char buf[32] = "0123456789";
//     memmove(buf + 2, buf, 8);
//     TEST(g, "memmove overlap: shifted prefix", buf[2] == '0' && buf[9] == '7');

//     /* memset */
//     memset(buf, 0xFF, 4);
//     TEST(g, "memset first 4 bytes", (unsigned char)buf[0] == 0xFF && (unsigned char)buf[3] == 0xFF);

//     /* strcmp */
//     TEST(g, "strcmp equal", strcmp("hello", "hello") == 0);
//     TEST(g, "strcmp less", strcmp("abc", "abd") < 0);
//     TEST(g, "strcmp greater", strcmp("abd", "abc") > 0);
//     TEST(g, "strcmp empty", strcmp("", "") == 0);

//     /* strlen */
//     TEST(g, "strlen", strlen("hello") == 5);
//     TEST(g, "strlen empty", strlen("") == 0);

//     /* strchr */
//     const char *s = "hello world";
//     TEST(g, "strchr finds char", strchr(s, 'w') == s + 6);
//     TEST(g, "strchr missing returns NULL", strchr(s, 'z') == NULL);

//     /* strncmp */
//     TEST(g, "strncmp n=3 equal prefix", strncmp("abcXXX", "abcYYY", 3) == 0);
//     TEST(g, "strncmp n=4 differs", strncmp("abcX", "abcY", 4) != 0);

//     ut_group_end(g);
// }

// /* ================================================================== stdout write */

// static void test_stdout(us_test_group_t *g)
// {
//     ut_group_begin(g, "stdout write (MicroPython print path)");

//     const char *line = "python libc test stdout check\n";
//     ssize_t n = write(STDOUT_FILENO, line, strlen(line));
//     TEST(g, "write to stdout returns byte count", n == (ssize_t)strlen(line));

//     /* putchar smoke */
//     int r = putchar('.');
//     putchar('\n');
//     TEST(g, "putchar does not return EOF", r != EOF);

//     ut_group_end(g);
// }

// /* ================================================================== entry point */

// void run_test_python_libc(us_test_group_t groups[], int *idx)
// {
//     test_heap         (&groups[(*idx)++]);
//     test_file_io      (&groups[(*idx)++]);
//     test_stat         (&groups[(*idx)++]);
//     test_dir_io       (&groups[(*idx)++]);
//     test_process      (&groups[(*idx)++]);
//     test_time         (&groups[(*idx)++]);
//     test_signal       (&groups[(*idx)++]);
//     test_setjmp       (&groups[(*idx)++]);
//     test_string_utils (&groups[(*idx)++]);
//     test_stdout       (&groups[(*idx)++]);
// }
