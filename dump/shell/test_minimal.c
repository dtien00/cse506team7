#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    const char *msg1 = "Parent process: PID=";
    const char *msg2 = "\n";
    
    pid_t my_pid = getpid();
    
    write(STDOUT_FILENO, msg1, 20);
    
    char pid_str[16];
    int i = 0;
    pid_t temp = my_pid;
    if (temp == 0) {
        pid_str[i++] = '0';
    } else {
        char rev[16];
        int j = 0;
        while (temp > 0) {
            rev[j++] = '0' + (temp % 10);
            temp /= 10;
        }
        while (j > 0) {
            pid_str[i++] = rev[--j];
        }
    }
    write(STDOUT_FILENO, pid_str, i);
    write(STDOUT_FILENO, msg2, 1);
    
    pid_t pid = fork();
    
    if (pid == 0) {
        const char *child_msg = "Child process running\n";
        write(STDOUT_FILENO, child_msg, 22);
        return 0;
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        const char *done_msg = "Child completed\n";
        write(STDOUT_FILENO, done_msg, 16);
    }
    
    return 0;
}
