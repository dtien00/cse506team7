/* test unistd.h syscalls: write, getpid, fork/wait */
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>

int main(void) {
	pid_t pid = getpid();
	printf("parent pid=%d\n", (int)pid);

	pid_t child = fork();
	if (child < 0) {
		printf("FAIL fork\n");
		return 1;
	}
	if (child == 0) {
		printf("child pid=%d ppid=%d\n", (int)getpid(), (int)getppid());
		exit(0);
	}
	int status;
	waitpid(child, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("FAIL child exit\n");
		return 1;
	}
	printf("PASS\n");
	return 0;
}
