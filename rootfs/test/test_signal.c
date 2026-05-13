/* test signal.h + setjmp.h */
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int got_sigusr1 = 0;

static void handler(int sig) {
	(void)sig;
	got_sigusr1 = 1;
}

int main(void) {
	struct sigaction sa;
	sa.sa_handler = handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);

	kill(getpid(), SIGUSR1);

	if (!got_sigusr1) {
		printf("FAIL: SIGUSR1 not received\n");
		return 1;
	}
	printf("PASS: got SIGUSR1\n");
	return 0;
}
