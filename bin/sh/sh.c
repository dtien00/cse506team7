#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_CMDS 16
#define MAX_JOBS 32

typedef struct shell_command {
    char *argv[MAX_ARGS];
    char *input_path;
    char *output_path;
} shell_command_t;

typedef enum {
    JOB_UNUSED = 0,
    JOB_RUNNING,     // Background job actively running
    JOB_STOPPED,     // Suspended via SIGTSTP or SIGSTOP
    JOB_DONE,        // Completed (not yet reported to user)
} job_state_t;

typedef struct {
    int           job_id;             // Job number [1..MAX_JOBS]
    pid_t         pgid;               // Process group ID of the job
    job_state_t   state;
    int           exit_status;        // Exit status once done
    char          command[256];        // Original command string
    int           is_current;          // 1 if this is the "current" (%) job
} job_t;

static job_t jobs[MAX_JOBS];
static int   next_job_id = 1;
static int   current_job = -1;        // Index into jobs[] for %+
static int   previous_job = -1;       // Index into jobs[] for %-

// Forward declarations for job helpers
int  job_add(pid_t pgid, const char *cmd);
void job_remove(int idx);
int  job_find_by_id(int job_id);
int  job_find_by_pgid(pid_t pgid);
void job_update_current(int new_current_idx);
void job_print(int idx);
void job_reap_done(void);
static int  job_resolve(const char *spec);
static int  builtin_bg(char **argv);
static int  builtin_fg(char **argv);
static int  builtin_jobs(char **argv);
static int  builtin_kill(char **argv);

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n';
}

static int is_special(char ch) {
    return ch == '|' || ch == '<' || ch == '>';
}

static char *next_token(char **cursor) {
    static char special_token[2];
    static char pending_special[2];
    static int has_pending_special = 0;
    char *p;
    char *start;

    if (cursor == NULL) {
        has_pending_special = 0;
        return NULL;
    }

    p = *cursor;

    if (has_pending_special) {
        has_pending_special = 0;
        return pending_special;
    }

    while (*p && is_space(*p)) {
        p++;
    }
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }

    if (is_special(*p)) {
        special_token[0] = *p++;
        special_token[1] = '\0';
        *cursor = p;
        return special_token;
    }

    start = p;
    while (*p && !is_space(*p) && !is_special(*p)) {
        p++;
    }
    if (is_space(*p)) {
        *p++ = '\0';
    } else if (is_special(*p)) {
        pending_special[0] = *p;
        pending_special[1] = '\0';
        has_pending_special = 1;
        *p++ = '\0';
    }
    *cursor = p;
    return start;
}

static int is_token(const char *token, char expected) {
    return token != NULL && token[0] == expected && token[1] == '\0';
}

static int parse_line(char *line, shell_command_t *cmdv) {
    char *cursor = line;
    char *token;
    int cmdc = 0;
    int argc = 0;

    next_token(NULL);
    memset(cmdv, 0, sizeof(shell_command_t) * MAX_CMDS);

    while ((token = next_token(&cursor)) != NULL) {
        if (cmdc >= MAX_CMDS) {
            return -1;
        }

        if (is_token(token, '|')) {
            if (argc == 0) {
                return -1;
            }
            cmdv[cmdc].argv[argc] = NULL;
            cmdc++;
            argc = 0;
            continue;
        }

        if (is_token(token, '<') || is_token(token, '>')) {
            char redirect_op = token[0];
            char *path = next_token(&cursor);
            if (path == NULL || is_token(path, '|') || is_token(path, '<') || is_token(path, '>')) {
                return -1;
            }

            if (cmdc >= MAX_CMDS) {
                return -1;
            }

            if (argc == 0 && cmdv[cmdc].argv[0] == NULL) {
                return -1;
            }

            if (redirect_op == '<') {
                if (cmdv[cmdc].input_path != NULL) {
                    return -1;
                }
                cmdv[cmdc].input_path = path;
            } else {
                if (cmdv[cmdc].output_path != NULL) {
                    return -1;
                }
                cmdv[cmdc].output_path = path;
            }
            continue;
        }

        if (argc >= MAX_ARGS - 1) {
            return -1;
        }
        cmdv[cmdc].argv[argc++] = token;
    }

    if (argc == 0) {
        return cmdc == 0 ? 0 : -1;
    }

    cmdv[cmdc].argv[argc] = NULL;
    return cmdc + 1;
}

static void build_exec_path(const char *command, char *path, size_t path_size) {
    if (command[0] != '/') {
        snprintf(path, path_size, "/bin/%s", command);
    } else {
        strncpy(path, command, path_size - 1);
        path[path_size - 1] = '\0';
    }
}

static int redirect_fd(int from_fd, int to_fd) {
    if (from_fd < 0) {
        return -1;
    }
    if (from_fd == to_fd) {
        return 0;
    }
    if (dup2(from_fd, to_fd) < 0) {
        return -1;
    }
    close(from_fd);
    return 0;
}

static int apply_redirections(const shell_command_t *cmd) {
    int fd;

    if (cmd->input_path) {
        fd = open(cmd->input_path, O_RDONLY, 0);
        if (fd < 0) {
            dprintf(STDERR_FILENO, "sh: cannot open '%s'\n", cmd->input_path);
            return -1;
        }
        if (redirect_fd(fd, STDIN_FILENO) < 0) {
            dprintf(STDERR_FILENO, "sh: dup2 failed for '%s'\n", cmd->input_path);
            close(fd);
            return -1;
        }
    }

    if (cmd->output_path) {
        fd = open(cmd->output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            dprintf(STDERR_FILENO, "sh: cannot open '%s'\n", cmd->output_path);
            return -1;
        }
        if (redirect_fd(fd, STDOUT_FILENO) < 0) {
            dprintf(STDERR_FILENO, "sh: dup2 failed for '%s'\n", cmd->output_path);
            close(fd);
            return -1;
        }
    }

    return 0;
}

static void exec_command(char **argv) {
    char path[256];

    // Restore default signal dispositions before exec (POSIX)
    signal(SIGINT,  SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);

    build_exec_path(argv[0], path, sizeof(path));
    execve(path, argv, NULL);
    dprintf(STDERR_FILENO, "sh: %s: command not found\n", argv[0]);
    exit(127);
}

static int builtin_cd(char **argv) {
    if (!argv[1]) {
        return 0;
    }
    if (chdir(argv[1]) < 0) {
        dprintf(STDERR_FILENO, "cd: cannot change directory to '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

static int builtin_exit(char **argv) {
    int code = 0;
    if (argv[1]) {
        code = atoi(argv[1]);
    }
    exit(code);
    return 0;
}

static int is_builtin(char **argv) {
    if (!argv[0]) {
        return 0;
    }
    return strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "exit") == 0 ||
           strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0 ||
           strcmp(argv[0], "jobs") == 0 || strcmp(argv[0], "kill") == 0;
}

static int run_builtin(char **argv) {
    if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd(argv);
    }
    if (strcmp(argv[0], "exit") == 0) {
        return builtin_exit(argv);
    }
    if (strcmp(argv[0], "bg") == 0) return builtin_bg(argv);
    if (strcmp(argv[0], "fg") == 0) return builtin_fg(argv);
    if (strcmp(argv[0], "jobs") == 0) return builtin_jobs(argv);
    if (strcmp(argv[0], "kill") == 0) return builtin_kill(argv);
    return -1;
}

static int execute_single(shell_command_t *cmd, int background,
                          const char *cmd_str) {
    int builtin_status;
    int saved_stdin = -1;
    int saved_stdout = -1;
    pid_t pid;
    int status;

    if (!cmd->argv[0]) {
        return 0;
    }

    // Built-ins always run in the foreground shell process
    if (is_builtin(cmd->argv) && !background) {
        if (!cmd->input_path && !cmd->output_path)
            return run_builtin(cmd->argv);

        if (cmd->input_path) {
            saved_stdin = dup(STDIN_FILENO);
            if (saved_stdin < 0) {
                dprintf(STDERR_FILENO, "sh: dup failed\n");
                return 1;
            }
        }
        if (cmd->output_path) {
            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout < 0) {
                dprintf(STDERR_FILENO, "sh: dup failed\n");
                if (saved_stdin >= 0) close(saved_stdin);
                return 1;
            }
        }

        if (apply_redirections(cmd) < 0) {
            if (saved_stdin >= 0) {
                dup2(saved_stdin, STDIN_FILENO);
                close(saved_stdin);
            }
            if (saved_stdout >= 0) {
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
            return 1;
        }

        builtin_status = run_builtin(cmd->argv);

        if (saved_stdin >= 0) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        return builtin_status;
    }

    pid = fork();
    if (pid < 0) {
        write(STDERR_FILENO, "fork failed\n", 12);
        return 1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        if (apply_redirections(cmd) < 0)
            _exit(1);
        exec_command(cmd->argv);
    }

    // Parent — ensure child pgid is set before we proceed
    setpgid(pid, pid);

    if (background) {
        int idx = job_add(pid, cmd_str);
        if (idx >= 0)
            printf("[%d] %d\n", jobs[idx].job_id, pid);
        return 0;
    }

    // Foreground: give terminal to child, wait with WUNTRACED
    tcsetpgrp(STDIN_FILENO, pid);
    waitpid(pid, &status, WUNTRACED);
    tcsetpgrp(STDIN_FILENO, getpgrp());

    if (WIFSTOPPED(status)) {
        int idx = job_add(pid, cmd_str);
        if (idx >= 0) {
            jobs[idx].state = JOB_STOPPED;
            printf("\n[%d]+  Stopped  %s\n", jobs[idx].job_id, jobs[idx].command);
        }
        return 0;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int execute_pipeline(char *line) {
    shell_command_t cmdv[MAX_CMDS];
    pid_t pids[MAX_CMDS];
    int cmdc;
    int prev_read = -1;
    int status = 0;
    int last_status = 0;
    int background = 0;

    // Save original command for the job table before parse_line mutates it
    char cmd_str[256];
    strncpy(cmd_str, line, sizeof(cmd_str) - 1);
    cmd_str[sizeof(cmd_str) - 1] = '\0';
    // Strip trailing newline
    size_t len = strlen(cmd_str);
    while (len > 0 && (cmd_str[len - 1] == '\n' || cmd_str[len - 1] == ' '))
        cmd_str[--len] = '\0';

    // Detect and strip trailing '&'
    if (len > 0 && cmd_str[len - 1] == '&') {
        background = 1;
        cmd_str[--len] = '\0';
        while (len > 0 && cmd_str[len - 1] == ' ')
            cmd_str[--len] = '\0';
        // Also strip from the line that parse_line will process
        char *amp = line;
        char *last_amp = NULL;
        while (*amp) { if (*amp == '&') last_amp = amp; amp++; }
        if (last_amp) *last_amp = ' ';
    }

    cmdc = parse_line(line, cmdv);
    if (cmdc == 0) return 0;
    if (cmdc < 0) {
        dprintf(STDERR_FILENO, "sh: invalid syntax\n");
        return 1;
    }

    if (cmdc == 1) {
        return execute_single(&cmdv[0], background, cmd_str);
    }

    // Multi-command pipeline
    pid_t pipeline_pgid = 0;

    for (int i = 0; i < cmdc; i++) {
        int pipefd[2] = {-1, -1};

        if (!cmdv[i].argv[0]) {
            dprintf(STDERR_FILENO, "sh: invalid syntax\n");
            if (prev_read >= 0) close(prev_read);
            return 1;
        }

        if (i < cmdc - 1 && pipe(pipefd) < 0) {
            write(STDERR_FILENO, "pipe failed\n", 12);
            if (prev_read >= 0) close(prev_read);
            return 1;
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            write(STDERR_FILENO, "fork failed\n", 12);
            if (prev_read >= 0) close(prev_read);
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);
            return 1;
        }

        if (pids[i] == 0) {
            /* All pipeline members share the first child's pgid */
            pid_t child_pgid = (i == 0) ? 0 : pipeline_pgid;
            setpgid(0, child_pgid);

            if (prev_read >= 0 && redirect_fd(prev_read, STDIN_FILENO) < 0) {
                _exit(1);
            }
            if (pipefd[1] >= 0 && redirect_fd(pipefd[1], STDOUT_FILENO) < 0) {
                _exit(1);
            }

            if (pipefd[0] >= 0) close(pipefd[0]);
            if (apply_redirections(&cmdv[i]) < 0) {
                _exit(1);
            }

            exec_command(cmdv[i].argv);
        }

        // Parent: set pgid for the first child (race-free)
        if (i == 0) {
            pipeline_pgid = pids[0];
            setpgid(pids[0], pids[0]);
        } else {
            setpgid(pids[i], pipeline_pgid);
        }

        if (prev_read >= 0) close(prev_read);
        if (pipefd[1] >= 0) close(pipefd[1]);
        prev_read = pipefd[0];
    }

    if (prev_read >= 0) close(prev_read);

    if (background) {
        int idx = job_add(pipeline_pgid, cmd_str);
        if (idx >= 0)
            printf("[%d] %d\n", jobs[idx].job_id, pids[0]);
        return 0;
    }

    // Foreground pipeline: give terminal to pipeline pgid
    tcsetpgrp(STDIN_FILENO, pipeline_pgid);

    for (int i = 0; i < cmdc; i++) {
        waitpid(pids[i], &status, WUNTRACED);
        last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    tcsetpgrp(STDIN_FILENO, getpgrp());

    // If the last child was stopped, register the pipeline as a stopped job
    if (WIFSTOPPED(status)) {
        int idx = job_add(pipeline_pgid, cmd_str);
        if (idx >= 0) {
            jobs[idx].state = JOB_STOPPED;
            printf("\n[%d]+  Stopped  %s\n", jobs[idx].job_id, jobs[idx].command);
        }
    }

    return last_status;
}

int main(void) {
    char line[MAX_LINE];
    int n;
    signal(SIGINT,  SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, SIG_IGN); 

    while (1) {
        job_reap_done();
        write(STDOUT_FILENO, "$ ", 2);

        n = read(STDIN_FILENO, line, sizeof(line) - 1);
        if (n == 0) break;        // EOF (Ctrl+D)
        if (n < 0)  continue;     // signal interrupted read — new prompt

        line[n] = '\0';

        if (line[0] == '\n' || line[0] == '\0') continue;

        execute_pipeline(line);
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Job table helpers
// ─────────────────────────────────────────────────────────────────────────────

int job_add(pid_t pgid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state == JOB_UNUSED) {
            jobs[i].job_id = next_job_id++;
            jobs[i].pgid   = pgid;
            jobs[i].state  = JOB_RUNNING;
            jobs[i].exit_status = 0;
            jobs[i].is_current  = 0;
            strncpy(jobs[i].command, cmd, sizeof(jobs[i].command) - 1);
            jobs[i].command[sizeof(jobs[i].command) - 1] = '\0';
            job_update_current(i);
            return i;
        }
    }
    dprintf(STDERR_FILENO, "sh: too many jobs\n");
    return -1;
}

void job_remove(int idx) {
    if (idx < 0 || idx >= MAX_JOBS) return;
    jobs[idx].state  = JOB_UNUSED;
    jobs[idx].job_id = 0;
    jobs[idx].pgid   = 0;
    jobs[idx].exit_status = 0;
    jobs[idx].command[0]  = '\0';
    jobs[idx].is_current  = 0;

    // Reassign current/previous if we just removed either one
    if (current_job == idx) {
        current_job = previous_job;
        previous_job = -1;
    } else if (previous_job == idx) {
        previous_job = -1;
    }
}

int job_find_by_id(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state != JOB_UNUSED && jobs[i].job_id == job_id)
            return i;
    }
    return -1;
}

int job_find_by_pgid(pid_t pgid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state != JOB_UNUSED && jobs[i].pgid == pgid)
            return i;
    }
    return -1;
}

int job_find_by_prefix(const char *prefix) {
    size_t len = strlen(prefix);
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state != JOB_UNUSED &&
            strncmp(jobs[i].command, prefix, len) == 0)
            return i;
    }
    return -1;
}

int job_find_by_substr(const char *substr) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state != JOB_UNUSED &&
            strstr(jobs[i].command, substr) != NULL)
            return i;
    }
    return -1;
}

void job_update_current(int new_current_idx) {
    if (new_current_idx < 0 || new_current_idx >= MAX_JOBS) return;
    if (current_job >= 0)
        jobs[current_job].is_current = 0;
    previous_job = current_job;
    current_job  = new_current_idx;
    jobs[current_job].is_current = 1;
}

void job_print(int idx) {
    if (idx < 0 || idx >= MAX_JOBS || jobs[idx].state == JOB_UNUSED) return;
    char mark = ' ';
    if (idx == current_job)       mark = '+';
    else if (idx == previous_job) mark = '-';

    const char *state_str;
    switch (jobs[idx].state) {
        case JOB_RUNNING: state_str = "Running";  break;
        case JOB_STOPPED: state_str = "Stopped";  break;
        case JOB_DONE:    state_str = "Done";     break;
        default:          state_str = "Unknown";  break;
    }
    printf("[%d]%c  %-10s %s\n", jobs[idx].job_id, mark, state_str,
           jobs[idx].command);
}

static int builtin_bg(char **argv) {
    int idx;
    if (argv[1]) {
        idx = job_resolve(argv[1]);   // Parse %N, %+, %-, %str, %?str
    } else {
        idx = current_job;            // Default: most recently stopped
    }

    if (idx < 0 || jobs[idx].state == JOB_UNUSED) {
        dprintf(STDERR_FILENO, "bg: no such job\n");
        return 1;
    }
    if (jobs[idx].state == JOB_RUNNING) {
        // Already running in background — success per POSIX
        return 0;
    }
    if (jobs[idx].state != JOB_STOPPED) {
        dprintf(STDERR_FILENO, "bg: job has terminated\n");
        return 1;
    }

    // Resume the process group with SIGCONT
    kill(-jobs[idx].pgid, SIGCONT);
    jobs[idx].state = JOB_RUNNING;

    // POSIX output format
    printf("[%d] %s\n", jobs[idx].job_id, jobs[idx].command);
    job_update_current(idx);
    return 0;
}

static int builtin_fg(char **argv) {
    int idx;
    if (argv[1]) {
        idx = job_resolve(argv[1]);
    } else {
        idx = current_job;
    }

    if (idx < 0 || jobs[idx].state == JOB_UNUSED) {
        dprintf(STDERR_FILENO, "fg: no such job\n");
        return 1;
    }
    if (jobs[idx].state == JOB_DONE) {
        dprintf(STDERR_FILENO, "fg: job has terminated\n");
        return 1;
    }

    // POSIX output: print the command
    printf("%s\n", jobs[idx].command);

    // Give the job the terminal
    tcsetpgrp(STDIN_FILENO, jobs[idx].pgid);

    // POSIX: always send SIGCONT when bringing to foreground, even if the
    // shell thinks the job is already running.  The kernel-side state may
    // disagree with the shell's cached state (e.g. stale cont_unreported
    // caused job_reap_done to flip JOB_RUNNING while process is stopped).
    kill(-jobs[idx].pgid, SIGCONT);
    jobs[idx].state = JOB_RUNNING;

    // Wait for it in the foreground
    int status = 0;
    int ret;
    do {
        ret = waitpid(-jobs[idx].pgid, &status, WUNTRACED);
    } while (ret < 0 /* && errno == EINTR */);

    // Take back the terminal
    tcsetpgrp(STDIN_FILENO, getpgrp());

    if (WIFSTOPPED(status)) {
        jobs[idx].state = JOB_STOPPED;
        printf("\n[%d]+  Stopped  %s\n", jobs[idx].job_id, jobs[idx].command);
        job_update_current(idx);
    } else {
        // Job finished — remove from table
        job_remove(idx);
    }

    return 0;
}

static int builtin_jobs(char **argv) {
    (void)argv;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state == JOB_UNUSED) continue;
        char mark = ' ';
        if (i == current_job) mark = '+';
        else if (i == previous_job) mark = '-';

        const char *state_str;
        switch (jobs[i].state) {
            case JOB_RUNNING: state_str = "Running"; break;
            case JOB_STOPPED: state_str = "Stopped"; break;
            case JOB_DONE:    state_str = "Done";    break;
            default:          state_str = "Unknown"; break;
        }
        printf("[%d]%c  %-10s %s\n", jobs[i].job_id, mark, state_str, jobs[i].command);
    }
    return 0;
}

static int builtin_kill(char **argv) {
    if (!argv[1]) {
        dprintf(STDERR_FILENO, "kill: usage: kill [-signal] pid|%%job\n");
        return 1;
    }

    int sig = SIGTERM;
    int arg_idx = 1;

    // Parse optional -SIGNAL
    if (argv[arg_idx][0] == '-' && argv[arg_idx][1] >= '0' &&
        argv[arg_idx][1] <= '9') {
        sig = atoi(argv[arg_idx] + 1);
        arg_idx++;
        if (!argv[arg_idx]) {
            dprintf(STDERR_FILENO, "kill: missing pid or job spec\n");
            return 1;
        }
    }

    // Process each target
    for (; argv[arg_idx]; arg_idx++) {
        const char *target = argv[arg_idx];

        if (target[0] == '%') {
            // Job spec — resolve via shell job table
            int idx = job_resolve(target);
            if (idx < 0 || jobs[idx].state == JOB_UNUSED) {
                dprintf(STDERR_FILENO, "kill: %s: no such job\n", target);
                return 1;
            }
            pid_t pgid = jobs[idx].pgid;

            // Send the requested signal to the process group
            kill(-pgid, sig);

            // A stopped process can't handle signals until continued.
            // POSIX shells send SIGCONT so the target actually dies.
            if (jobs[idx].state == JOB_STOPPED && sig != SIGCONT)
                kill(-pgid, SIGCONT);
        } else {
            // Bare PID
            pid_t pid = (pid_t)atoi(target);
            if (pid == 0) {
                dprintf(STDERR_FILENO, "kill: invalid pid: %s\n", target);
                return 1;
            }
            kill(pid, sig);
        }
    }
    return 0;
}

static int job_resolve(const char *spec) {
    if (!spec || spec[0] == '\0') return current_job;

    if (spec[0] == '%') {
        spec++;
        if (spec[0] == '%' || spec[0] == '+' || spec[0] == '\0') return current_job;
        if (spec[0] == '-') return previous_job;
        if (spec[0] == '?') {
            // Substring match
            for (int i = 0; i < MAX_JOBS; i++) {
                if (jobs[i].state != JOB_UNUSED && strstr(jobs[i].command, spec + 1))
                    return i;
            }
            return -1;
        }
        // Try numeric
        int n = atoi(spec);
        if (n > 0) return job_find_by_id(n);
        // Prefix match
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].state != JOB_UNUSED &&
                strncmp(jobs[i].command, spec, strlen(spec)) == 0)
                return i;
        }
    } else {
        // Bare number
        int n = atoi(spec);
        if (n > 0) return job_find_by_id(n);
    }
    return -1;
}

void job_reap_done(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        int idx = job_find_by_pgid(pid);
        if (idx < 0) continue;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            jobs[idx].state = JOB_DONE;
            jobs[idx].exit_status = status;
            printf("[%d]   Done  %s\n", jobs[idx].job_id, jobs[idx].command);
            job_remove(idx);
        } else if (WIFSTOPPED(status)) {
            jobs[idx].state = JOB_STOPPED;
            printf("[%d]+  Stopped  %s\n", jobs[idx].job_id, jobs[idx].command);
            job_update_current(idx);
        } else if (WIFCONTINUED(status)) {
            jobs[idx].state = JOB_RUNNING;
        }
    }
}