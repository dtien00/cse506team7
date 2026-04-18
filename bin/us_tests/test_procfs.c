#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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

static int parse_status_field(const char *status, const char *key, char *out, size_t out_size) {
    const size_t key_len = strlen(key);
    const char *line = status;
    while (*line) {
        if (strncmp(line, key, key_len) == 0 && line[key_len] == ':') {
            const char *value = line + key_len + 1;
            while (*value == ' ') value++;
            size_t len = 0;
            while (value[len] && value[len] != '\n') len++;
            if (len + 1 > out_size)
                len = out_size - 1;
            memcpy(out, value, len);
            out[len] = '\0';
            return 0;
        }
        while (*line && *line != '\n')
            line++;
        if (*line == '\n')
            line++;
    }
    if (out_size > 0)
        out[0] = '\0';
    return -1;
}

void run_test_procfs(us_test_group_t *group) {
    ut_group_begin(group, "procfs");

    DIR *proc = opendir("/proc");
    TEST(group, "opendir(\"/proc\") succeeds", proc != NULL);
    if (!proc) {
        SKIP(group, "readdir(/proc): returns one or more entries", "opendir failed");
        SKIP(group, "opendir(/proc/self) succeeds", "opendir failed");
        SKIP(group, "open(/proc/self/status) succeeds", "opendir failed");
        SKIP(group, "open invalid pid returns ENOENT", "opendir failed");
        ut_group_end(group);
        return;
    }

    int numeric_entries = 0;
    int self_seen = 0;
    int status_seen = 0;
    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        if (strcmp(entry->d_name, "self") == 0)
            self_seen = 1;
        int valid = 1;
        for (size_t i = 0; entry->d_name[i]; i++) {
            if (entry->d_name[i] < '0' || entry->d_name[i] > '9') {
                valid = 0;
                break;
            }
        }
        if (valid)
            numeric_entries++;
    }
    closedir(proc);

    TEST(group, "readdir(/proc) returns at least one numeric entry", numeric_entries > 0);
    TEST(group, "readdir(/proc) does not expose \"self\" as a direct entry", self_seen == 0);

    DIR *self_dir = opendir("/proc/self");
    TEST(group, "opendir(\"/proc/self\") succeeds", self_dir != NULL);
    if (self_dir) {
        struct dirent *self_entry = readdir(self_dir);
        TEST(group, "readdir(/proc/self) returns at least one entry", self_entry != NULL);
        if (self_entry) {
            TEST(group, "readdir(/proc/self) entry name is \"status\"",
                 strcmp(self_entry->d_name, "status") == 0);
            TEST(group, "readdir(/proc/self) entry is regular file",
                 self_entry->d_type == DT_REG);
            status_seen = 1;
        }
        closedir(self_dir);
    } else {
        SKIP(group, "readdir(/proc/self) entry name is \"status\"", "opendir failed");
        SKIP(group, "readdir(/proc/self) entry is regular file", "opendir failed");
    }

    TEST(group, "status file found in /proc/self directory", status_seen == 1);

    char status_buf[512];
    int n = read_to_buffer("/proc/self/status", status_buf, sizeof(status_buf));
    TEST(group, "open/read /proc/self/status succeeds", n > 0);
    if (n > 0) {
        char pid_str[32];
        char name_str[64];
        char state_str[32];
        char ppid_str[32];

        int pid_ok = parse_status_field(status_buf, "pid", pid_str, sizeof(pid_str)) == 0;
        int ppid_ok = parse_status_field(status_buf, "ppid", ppid_str, sizeof(ppid_str)) == 0;
        int name_ok = parse_status_field(status_buf, "name", name_str, sizeof(name_str)) == 0;
        int state_ok = parse_status_field(status_buf, "state", state_str, sizeof(state_str)) == 0;

        TEST(group, "status contains pid line", pid_ok);
        TEST(group, "status contains ppid line", ppid_ok);
        TEST(group, "status contains name line", name_ok);
        TEST(group, "status contains state line", state_ok);

        if (pid_ok) {
            TEST(group, "pid in status matches getpid()",
                 atoi(pid_str) == getpid());
        }
        if (ppid_ok) {
            TEST(group, "ppid in status is non-negative",
                 atoi(ppid_str) >= 0);
        }
        if (name_ok) {
            TEST(group, "name in status is non-empty", name_str[0] != '\0');
        }
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        SKIP(group, "procfs child status visible while alive", "pipe failed");
        SKIP(group, "procfs child status gone after wait", "pipe failed");
        ut_group_end(group);
        return;
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        SKIP(group, "procfs child status visible while alive", "fork failed");
        SKIP(group, "procfs child status gone after wait", "fork failed");
        ut_group_end(group);
        return;
    }

    if (child == 0) {
        close(pipefd[1]);
        char c;
        read(pipefd[0], &c, 1);
        close(pipefd[0]);
        _exit(0);
    }

    close(pipefd[0]);
    char child_path[64];
    snprintf(child_path, sizeof(child_path), "/proc/%d/status", child);

    int child_n = read_to_buffer(child_path, status_buf, sizeof(status_buf));
    TEST(group, "open/read /proc/<child>/status succeeds while child alive", child_n > 0);
    if (child_n > 0) {
        char child_pid_str[32];
        TEST(group, "child status pid matches child pid",
             parse_status_field(status_buf, "pid", child_pid_str, sizeof(child_pid_str)) == 0 &&
             atoi(child_pid_str) == child);
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
