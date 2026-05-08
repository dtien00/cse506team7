#include "us_test_common.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int write_script(const char *path, const char *body) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) return -1;
    ssize_t len = (ssize_t)strlen(body);
    if (write(fd, body, (size_t)len) != len) { close(fd); return -1; }
    return close(fd);
}

/* #!/bin/sh script that exits with a known code */
static void test_shebang_basic(us_test_group_t *tg) {
    const char *path = "/mnt/shebang_basic.sh";
    if (write_script(path, "#!/bin/sh\nexit 42\n") < 0) {
        SKIP(tg, "shebang basic: script write failed", "could not create script");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)path, NULL };
        execve(path, av, NULL);
        _exit(99);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    TEST(tg, "shebang: #!/bin/sh script exits with declared exit code",
         WIFEXITED(st) && WEXITSTATUS(st) == 42);
    unlink(path);
}

/* #!/bin/sh script invoked with extra argv — chain must not break */
static void test_shebang_argv_passthrough(us_test_group_t *tg) {
    const char *path = "/mnt/shebang_argv.sh";
    if (write_script(path, "#!/bin/sh\nexit 7\n") < 0) {
        SKIP(tg, "shebang argv: script write failed", "could not create script");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)path, "extra_arg", NULL };
        execve(path, av, NULL);
        _exit(99);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    TEST(tg, "shebang: extra argv[1..] does not break exec chain",
         WIFEXITED(st) && WEXITSTATUS(st) == 7);
    unlink(path);
}

/* interpreter path that does not exist — execve must fail */
static void test_shebang_bad_interp(us_test_group_t *tg) {
    const char *path = "/mnt/shebang_badinterp.sh";
    if (write_script(path, "#!/no/such/interpreter\necho hi\n") < 0) {
        SKIP(tg, "shebang bad interp: script write failed", "could not create script");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)path, NULL };
        execve(path, av, NULL);
        _exit(0);   /* execve failed → reach here */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    /* execve returns -1; child falls through to _exit(0) */
    TEST(tg, "shebang: missing interpreter causes execve to fail",
         WIFEXITED(st) && WEXITSTATUS(st) == 0);
    unlink(path);
}

/* plain text file (no shebang, no ELF) — execve must fail */
static void test_shebang_plain_text(us_test_group_t *tg) {
    const char *path = "/mnt/shebang_plain.txt";
    if (write_script(path, "just a text file\n") < 0) {
        SKIP(tg, "shebang plain text: write failed", "could not create file");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)path, NULL };
        execve(path, av, NULL);
        _exit(0);   /* execve failed */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    TEST(tg, "shebang: non-ELF non-shebang file: execve fails",
         WIFEXITED(st) && WEXITSTATUS(st) == 0);
    unlink(path);
}

/* #!/bin/python script execved directly — exercises the shebang→python path */
static void test_shebang_python(us_test_group_t *tg) {
    if (access("/bin/python", X_OK) != 0) {
        SKIP(tg, "shebang python: /bin/python not present", "/bin/python missing");
        return;
    }
    const char *path = "/mnt/shebang_python.py";
    if (write_script(path, "#!/bin/python\nimport sys\nsys.exit(37)\n") < 0) {
        SKIP(tg, "shebang python: script write failed", "could not create script");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)path, NULL };
        execve(path, av, NULL);
        _exit(99);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    TEST(tg, "shebang: #!/bin/python script exits with declared exit code",
         WIFEXITED(st) && WEXITSTATUS(st) == 37);
    unlink(path);
}

/* optional argument on the shebang line is passed to interpreter */
static void test_shebang_opt_arg(us_test_group_t *tg) {
    const char *path = "/mnt/shebang_optarg.sh";
    /* /bin/sh ignores unrecognised flags; script still runs */
    if (write_script(path, "#!/bin/sh -e\nexit 5\n") < 0) {
        SKIP(tg, "shebang opt arg: write failed", "could not create script");
        return;
    }
    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)path, NULL };
        execve(path, av, NULL);
        _exit(99);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    TEST(tg, "shebang: optional interpreter argument accepted",
         WIFEXITED(st) && WEXITSTATUS(st) == 5);
    unlink(path);
}

void run_test_exec_shebang(us_test_group_t *group) {
    ut_group_begin(group, "exec_shebang");
    test_shebang_basic(group);
    test_shebang_argv_passthrough(group);
    test_shebang_bad_interp(group);
    test_shebang_plain_text(group);
    test_shebang_opt_arg(group);
    test_shebang_python(group);
    ut_group_end(group);
}
