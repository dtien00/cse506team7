#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <dirent.h>
#include "errno.h"
#include "us_test_common.h"

static int read_to_buffer(const char *path, char *buf, size_t bufsize) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    int n = read(fd, buf, bufsize - 1);
    int saved_errno = errno;
    if (n >= 0)
        buf[n] = '\0';
    close(fd);
    errno = saved_errno;
    return n;
}

// Parse "Label:\t<value>\n" format (Linux /proc style).
// Accepts both tab and space after colon.
static int parse_status_field(const char *status, const char *key, char *out, size_t out_size) {
    const size_t key_len = strlen(key);
    const char *line = status;
    while (*line) {
        if (strncmp(line, key, key_len) == 0 && line[key_len] == ':') {
            const char *value = line + key_len + 1;
            while (*value == '\t' || *value == ' ') value++;
            size_t len = 0;
            while (value[len] && value[len] != '\n') len++;
            if (len + 1 > out_size) len = out_size - 1;
            memcpy(out, value, len);
            out[len] = '\0';
            return 0;
        }
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }
    if (out_size > 0) out[0] = '\0';
    return -1;
}

void run_test_procfs(us_test_group_t *group) {
    ut_group_begin(group, "procfs");

    // ── /proc directory ────────────────────────────────────────────────────

    DIR *proc = opendir("/proc");
    TEST(group, "opendir(\"/proc\") succeeds", proc != NULL);
    if (!proc) {
        SKIP(group, "readdir(/proc) returns numeric entries",  "opendir failed");
        SKIP(group, "readdir(/proc) exposes self entry",       "opendir failed");
        SKIP(group, "readdir(/proc) exposes meminfo entry",    "opendir failed");
        SKIP(group, "opendir(/proc/self) succeeds",            "opendir failed");
        SKIP(group, "open(/proc/self/status) succeeds",        "opendir failed");
        SKIP(group, "open invalid pid returns error",          "opendir failed");
        ut_group_end(group);
        return;
    }

    int numeric_entries = 0;
    int self_seen    = 0;
    int meminfo_seen = 0;
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "self")    == 0) { self_seen    = 1; continue; }
        if (strcmp(entry->d_name, "meminfo") == 0) { meminfo_seen = 1; continue; }
        int valid = 1;
        for (size_t i = 0; entry->d_name[i]; i++) {
            if (entry->d_name[i] < '0' || entry->d_name[i] > '9') { valid = 0; break; }
        }
        if (valid) numeric_entries++;
    }
    closedir(proc);

    TEST(group, "readdir(/proc) returns at least one numeric entry", numeric_entries > 0);
    TEST(group, "readdir(/proc) exposes \"self\" entry",             self_seen    == 1);
    TEST(group, "readdir(/proc) exposes \"meminfo\" entry",          meminfo_seen == 1);

    // ── /proc/self directory ───────────────────────────────────────────────

    DIR *self_dir = opendir("/proc/self");
    TEST(group, "opendir(\"/proc/self\") succeeds", self_dir != NULL);
    if (self_dir) {
        int has_status = 0, has_stat = 0, has_cwd = 0, has_exe = 0, has_root = 0;
        int any_entry = 0;
        struct dirent *e;
        while ((e = readdir(self_dir)) != NULL) {
            any_entry = 1;
            if (strcmp(e->d_name, "status") == 0) has_status = 1;
            if (strcmp(e->d_name, "stat")   == 0) has_stat   = 1;
            if (strcmp(e->d_name, "cwd")    == 0) has_cwd    = 1;
            if (strcmp(e->d_name, "exe")    == 0) has_exe    = 1;
            if (strcmp(e->d_name, "root")   == 0) has_root   = 1;
        }
        closedir(self_dir);
        TEST(group, "readdir(/proc/self) returns at least one entry", any_entry);
        TEST(group, "readdir(/proc/self) contains status", has_status);
        TEST(group, "readdir(/proc/self) contains stat",   has_stat);
        TEST(group, "readdir(/proc/self) contains cwd",    has_cwd);
        TEST(group, "readdir(/proc/self) contains exe",    has_exe);
        TEST(group, "readdir(/proc/self) contains root",   has_root);
    } else {
        SKIP(group, "readdir(/proc/self) returns at least one entry", "opendir failed");
        SKIP(group, "readdir(/proc/self) contains status", "opendir failed");
        SKIP(group, "readdir(/proc/self) contains stat",   "opendir failed");
        SKIP(group, "readdir(/proc/self) contains cwd",    "opendir failed");
        SKIP(group, "readdir(/proc/self) contains exe",    "opendir failed");
        SKIP(group, "readdir(/proc/self) contains root",   "opendir failed");
    }

    // ── /proc/self/status ─────────────────────────────────────────────────

    char status_buf[1024];
    int n = read_to_buffer("/proc/self/status", status_buf, sizeof(status_buf));
    TEST(group, "open/read /proc/self/status succeeds", n > 0);
    if (n > 0) {
        char pid_str[32], ppid_str[32], tgid_str[32];
        char pgrp_str[32], pgid_str[32], sid_str[32];
        char name_str[64], state_str[64], threads_str[32];
        char vmsize_str[64], vmstk_str[64], vmdata_str[64], vmexe_str[64];
        char kstack_str[64], sigpnd_str[32], sigblk_str[32];

        int pid_ok     = parse_status_field(status_buf, "Pid",         pid_str,     sizeof(pid_str))     == 0;
        int ppid_ok    = parse_status_field(status_buf, "PPid",        ppid_str,    sizeof(ppid_str))    == 0;
        int tgid_ok    = parse_status_field(status_buf, "Tgid",        tgid_str,    sizeof(tgid_str))    == 0;
        int pgrp_ok    = parse_status_field(status_buf, "Pgrp",        pgrp_str,    sizeof(pgrp_str))    == 0;
        int pgid_ok    = parse_status_field(status_buf, "Pgid",        pgid_str,    sizeof(pgid_str))    == 0;
        int sid_ok     = parse_status_field(status_buf, "Sid",         sid_str,     sizeof(sid_str))     == 0;
        int name_ok    = parse_status_field(status_buf, "Name",        name_str,    sizeof(name_str))    == 0;
        int state_ok   = parse_status_field(status_buf, "State",       state_str,   sizeof(state_str))   == 0;
        int threads_ok = parse_status_field(status_buf, "Threads",     threads_str, sizeof(threads_str)) == 0;
        int vmsize_ok  = parse_status_field(status_buf, "VmSize",      vmsize_str,  sizeof(vmsize_str))  == 0;
        int vmstk_ok   = parse_status_field(status_buf, "VmStk",       vmstk_str,   sizeof(vmstk_str))   == 0;
        int vmdata_ok  = parse_status_field(status_buf, "VmData",      vmdata_str,  sizeof(vmdata_str))  == 0;
        int vmexe_ok   = parse_status_field(status_buf, "VmExe",       vmexe_str,   sizeof(vmexe_str))   == 0;
        int kstack_ok  = parse_status_field(status_buf, "KernelStack", kstack_str,  sizeof(kstack_str))  == 0;
        int sigpnd_ok  = parse_status_field(status_buf, "SigPnd",      sigpnd_str,  sizeof(sigpnd_str))  == 0;
        int sigblk_ok  = parse_status_field(status_buf, "SigBlk",      sigblk_str,  sizeof(sigblk_str))  == 0;

        TEST(group, "status contains Pid",         pid_ok);
        TEST(group, "status contains PPid",        ppid_ok);
        TEST(group, "status contains Tgid",        tgid_ok);
        TEST(group, "status contains Pgrp",        pgrp_ok);
        TEST(group, "status contains Pgid",        pgid_ok);
        TEST(group, "status contains Sid",         sid_ok);
        TEST(group, "status contains Name",        name_ok);
        TEST(group, "status contains State",       state_ok);
        TEST(group, "status contains Threads",     threads_ok);
        TEST(group, "status contains VmSize",      vmsize_ok);
        TEST(group, "status contains VmStk",       vmstk_ok);
        TEST(group, "status contains VmData",      vmdata_ok);
        TEST(group, "status contains VmExe",       vmexe_ok);
        TEST(group, "status contains KernelStack", kstack_ok);
        TEST(group, "status contains SigPnd",      sigpnd_ok);
        TEST(group, "status contains SigBlk",      sigblk_ok);

        if (pid_ok)
            TEST(group, "Pid in status matches getpid()", atoi(pid_str) == getpid());
        if (pid_ok && tgid_ok)
            TEST(group, "Tgid equals Pid (single-threaded)",
                 atoi(tgid_str) == atoi(pid_str));
        if (ppid_ok)
            TEST(group, "PPid in status is non-negative", atoi(ppid_str) >= 0);
        if (pgrp_ok && pgid_ok)
            TEST(group, "Pgrp equals Pgid", strcmp(pgrp_str, pgid_str) == 0);
        if (sid_ok)
            TEST(group, "Sid is non-negative", atoi(sid_str) >= 0);
        if (name_ok)
            TEST(group, "Name in status is non-empty", name_str[0] != '\0');
        if (threads_ok)
            TEST(group, "Threads is \"1\"", strcmp(threads_str, "1") == 0);
        if (vmsize_ok) {
            int vl = (int)strlen(vmsize_str);
            TEST(group, "VmSize ends with \" kB\"",
                 vl >= 3 && vmsize_str[vl-2] == 'k' && vmsize_str[vl-1] == 'B');
        }
        if (vmstk_ok) {
            int vl = (int)strlen(vmstk_str);
            TEST(group, "VmStk ends with \" kB\"",
                 vl >= 3 && vmstk_str[vl-2] == 'k' && vmstk_str[vl-1] == 'B');
        }
        if (vmdata_ok) {
            int vl = (int)strlen(vmdata_str);
            TEST(group, "VmData ends with \" kB\"",
                 vl >= 3 && vmdata_str[vl-2] == 'k' && vmdata_str[vl-1] == 'B');
        }
        if (vmexe_ok) {
            int vl = (int)strlen(vmexe_str);
            TEST(group, "VmExe ends with \" kB\"",
                 vl >= 3 && vmexe_str[vl-2] == 'k' && vmexe_str[vl-1] == 'B');
        }
        if (kstack_ok)
            TEST(group, "KernelStack is \"4 kB\"", strcmp(kstack_str, "4 kB") == 0);
        if (sigpnd_ok)
            TEST(group, "SigPnd is 16 hex chars", (int)strlen(sigpnd_str) == 16);
        if (sigblk_ok)
            TEST(group, "SigBlk is 16 hex chars", (int)strlen(sigblk_str) == 16);
    }

    // ── /proc/self/status: VmSize grows after mmap ────────────────────────
    // Reads status twice w/ a fresh 2 MB anonymous mapping held between
    // reads. Catches broken VMA accounting (status that doesn't update
    // when the address space changes).
    {
        char buf1[1024], buf2[1024], v1[64], v2[64];
        int n1 = read_to_buffer("/proc/self/status", buf1, sizeof(buf1));
        long kb1 = -1, kb2 = -1;
        int v1_ok = (n1 > 0 && parse_status_field(buf1, "VmSize", v1, sizeof(v1)) == 0);
        if (v1_ok) kb1 = atol(v1);

        void *p = mmap(NULL, 2UL * 1024 * 1024,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        int mapped = (p != (void *)-1);
        if (mapped) {
            ((volatile char *)p)[0] = 1;  // force a fault
            int n2 = read_to_buffer("/proc/self/status", buf2, sizeof(buf2));
            int v2_ok = (n2 > 0 && parse_status_field(buf2, "VmSize", v2, sizeof(v2)) == 0);
            if (v2_ok) kb2 = atol(v2);
            munmap(p, 2UL * 1024 * 1024);

            TEST(group, "VmSize grows after 2 MB mmap (>= +1024 kB)",
                 kb1 > 0 && kb2 > 0 && (kb2 - kb1) >= 1024);
        } else {
            SKIP(group, "VmSize grows after 2 MB mmap (>= +1024 kB)", "mmap failed");
        }
    }

    // ── /proc/self/stat ────────────────────────────────────────────────────

    {
        char stat_buf[512];
        int sn = read_to_buffer("/proc/self/stat", stat_buf, sizeof(stat_buf));
        TEST(group, "open/read /proc/self/stat succeeds", sn > 0);
        if (sn > 0) {
            TEST(group, "stat starts with digit", stat_buf[0] >= '0' && stat_buf[0] <= '9');
            TEST(group, "stat first field is own pid", atoi(stat_buf) == getpid());
            TEST(group, "stat ends with newline",
                 sn > 0 && stat_buf[sn - 1] == '\n');
            // Find "(comm)" field
            char *open_p  = strchr(stat_buf, '(');
            char *close_p = strchr(stat_buf, ')');
            TEST(group, "stat contains (comm) field", open_p != NULL && close_p != NULL && close_p > open_p);
            if (open_p && close_p && close_p > open_p) {
                // State char after ")" space
                char state_c = *(close_p + 2);
                TEST(group, "stat state char is R, S, D, T, or Z",
                     state_c == 'R' || state_c == 'S' || state_c == 'D' ||
                     state_c == 'T' || state_c == 'Z');
                // Field 4 after state char must be PPid — non-negative int
                const char *rest = close_p + 3; // skip ") <state> "
                while (*rest == ' ') rest++;
                TEST(group, "stat field 4 (ppid) is non-negative integer",
                     atoi(rest) >= 0);
                // Count remaining space-separated fields — Linux/this impl
                // emit a known minimum after (comm)
                int fields = 1; // state char counted
                for (const char *q = close_p + 2; *q && *q != '\n'; q++)
                    if (*q == ' ') fields++;
                TEST(group, "stat has >= 20 fields after (comm)", fields >= 20);
            }
        }
    }

    // ── /proc/self/smaps ───────────────────────────────────────────────────

    {
        char smaps_buf[512];
        int sm = read_to_buffer("/proc/self/smaps", smaps_buf, sizeof(smaps_buf));
        TEST(group, "open/read /proc/self/smaps succeeds", sm > 0);
        if (sm > 0) {
            TEST(group, "smaps contains VmSize field",  strstr(smaps_buf, "VmSize")  != NULL);
            TEST(group, "smaps contains VmStack field", strstr(smaps_buf, "VmStack") != NULL);
            TEST(group, "smaps contains VmData field",  strstr(smaps_buf, "VmData")  != NULL);
            TEST(group, "smaps contains VmExe field",   strstr(smaps_buf, "VmExe")   != NULL);
            TEST(group, "smaps contains KernelStack",   strstr(smaps_buf, "KernelStack") != NULL);
        }
    }

    // ── /proc/meminfo ──────────────────────────────────────────────────────

    {
        char mi_buf[512];
        int mn = read_to_buffer("/proc/meminfo", mi_buf, sizeof(mi_buf));
        TEST(group, "open/read /proc/meminfo succeeds", mn > 0);
        if (mn > 0) {
            char total_str[32], free_str[32], avail_str[32];
            char buf_str[32], cache_str[32], swt_str[32], swf_str[32];
            int total_ok = parse_status_field(mi_buf, "MemTotal",     total_str, sizeof(total_str)) == 0;
            int free_ok  = parse_status_field(mi_buf, "MemFree",      free_str,  sizeof(free_str))  == 0;
            int avail_ok = parse_status_field(mi_buf, "MemAvailable", avail_str, sizeof(avail_str)) == 0;
            int buf_ok   = parse_status_field(mi_buf, "Buffers",      buf_str,   sizeof(buf_str))   == 0;
            int cache_ok = parse_status_field(mi_buf, "Cached",       cache_str, sizeof(cache_str)) == 0;
            int swt_ok   = parse_status_field(mi_buf, "SwapTotal",    swt_str,   sizeof(swt_str))   == 0;
            int swf_ok   = parse_status_field(mi_buf, "SwapFree",     swf_str,   sizeof(swf_str))   == 0;
            TEST(group, "meminfo contains MemTotal",     total_ok);
            TEST(group, "meminfo contains MemFree",      free_ok);
            TEST(group, "meminfo contains MemAvailable", avail_ok);
            TEST(group, "meminfo contains Buffers",      buf_ok);
            TEST(group, "meminfo contains Cached",       cache_ok);
            TEST(group, "meminfo contains SwapTotal",    swt_ok);
            TEST(group, "meminfo contains SwapFree",     swf_ok);
            if (total_ok && free_ok) {
                long total_kb = atol(total_str);
                long free_kb  = atol(free_str);
                TEST(group, "MemTotal > 0",        total_kb > 0);
                TEST(group, "MemFree <= MemTotal", free_kb  <= total_kb);
            }
            if (total_ok && avail_ok) {
                TEST(group, "MemAvailable <= MemTotal",
                     atol(avail_str) <= atol(total_str));
            }
        }
    }

    // ── /proc/self/cwd, /proc/self/root, /proc/self/exe symlinks ───────────

    {
        char link_buf[256];
        int lr = readlink("/proc/self/cwd", link_buf, sizeof(link_buf) - 1);
        TEST(group, "readlink(/proc/self/cwd) succeeds", lr > 0);
        if (lr > 0) {
            link_buf[lr] = '\0';
            TEST(group, "cwd readlink starts with /", link_buf[0] == '/');
        }
    }
    {
        char link_buf[256];
        int lr = readlink("/proc/self/root", link_buf, sizeof(link_buf) - 1);
        TEST(group, "readlink(/proc/self/root) succeeds", lr > 0);
        if (lr > 0) {
            link_buf[lr] = '\0';
            TEST(group, "root readlink starts with /", link_buf[0] == '/');
        }
    }
    {
        // exe: openable; content may be empty until first exec
        int fd = open("/proc/self/exe", O_RDONLY);
        TEST(group, "open(/proc/self/exe) succeeds", fd >= 0);
        if (fd >= 0) close(fd);
    }

    // ── /proc/self via readlink ────────────────────────────────────────────

    {
        char link_buf[64];
        int lr = readlink("/proc/self", link_buf, sizeof(link_buf) - 1);
        TEST(group, "readlink(/proc/self) succeeds", lr > 0);
        if (lr > 0) {
            link_buf[lr] = '\0';
            int val = 0, valid = 1;
            for (int i = 0; i < lr; i++) {
                if (link_buf[i] < '0' || link_buf[i] > '9') { valid = 0; break; }
                val = val * 10 + (link_buf[i] - '0');
            }
            TEST(group, "readlink(/proc/self) is numeric pid", valid);
            TEST(group, "readlink(/proc/self) matches getpid()", val == getpid());
        }
    }

    // ── fork/wait lifecycle ────────────────────────────────────────────────

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        SKIP(group, "procfs child status visible while alive", "pipe failed");
        SKIP(group, "procfs child status gone after wait",     "pipe failed");
        ut_group_end(group);
        return;
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]); close(pipefd[1]);
        SKIP(group, "procfs child status visible while alive", "fork failed");
        SKIP(group, "procfs child status gone after wait",     "fork failed");
        ut_group_end(group);
        return;
    }

    if (child == 0) {
        close(pipefd[1]);
        char c; read(pipefd[0], &c, 1);
        close(pipefd[0]);
        _exit(0);
    }

    close(pipefd[0]);
    char child_path[64];
    snprintf(child_path, sizeof(child_path), "/proc/%d/status", child);

    // Poll until child has actually scheduled in and blocked on the pipe read.
    // Without this, parent can race ahead of fork()→exec path and observe the
    // child still in R before it reaches sys_read().
    char child_pid_str[32], child_ppid_str[32], child_state_str[64];
    int child_n = 0, cpid_ok = 0, cppid_ok = 0, cst_ok = 0;
    for (int i = 0; i < 50; i++) {
        child_n = read_to_buffer(child_path, status_buf, sizeof(status_buf));
        if (child_n > 0) {
            cpid_ok  = parse_status_field(status_buf, "Pid",   child_pid_str,   sizeof(child_pid_str))   == 0;
            cppid_ok = parse_status_field(status_buf, "PPid",  child_ppid_str,  sizeof(child_ppid_str))  == 0;
            cst_ok   = parse_status_field(status_buf, "State", child_state_str, sizeof(child_state_str)) == 0;
            if (cst_ok && child_state_str[0] == 'S') break;
        }
        usleep(10000);
    }
    TEST(group, "open/read /proc/<child>/status while child alive", child_n > 0);
    if (child_n > 0) {
        TEST(group, "child status Pid matches child pid",
             cpid_ok && atoi(child_pid_str) == child);
        TEST(group, "child PPid matches our pid",
             cppid_ok && atoi(child_ppid_str) == getpid());
        // child is blocked on read() from pipe — should be in S state
        TEST(group, "child state is S (sleeping in pipe read)",
             cst_ok && child_state_str[0] == 'S');
    }

    write(pipefd[1], "x", 1);
    close(pipefd[1]);
    waitpid(child, NULL, 0);

    int child_n2 = read_to_buffer(child_path, status_buf, sizeof(status_buf));
    TEST(group, "open /proc/<child>/status fails after child reaped", child_n2 < 0);

    int invalid = read_to_buffer("/proc/999999/status", status_buf, sizeof(status_buf));
    TEST(group, "open invalid /proc pid returns error", invalid < 0);

    ut_group_end(group);
}
