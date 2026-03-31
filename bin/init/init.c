#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

// static int spawn_echo(const char *message) {
//   pid_t pid = fork();
//   if (pid < 0) {
//     printf("[init] fork failed\n");
//     return -1;
//   }

//   if (pid == 0) {
//     char *argv[] = { "echo", (char *)message, NULL };
//     execve("/bin/echo", argv, NULL);
//     printf("[init] exec failed for /bin/echo\n");
//     exit(1);
//   }

//   printf("[init] spawned child %d\n", pid);
//   return 0;
// }

int main(int argc, char *argv[]) {
  // Open console as stdin/stdout/stderr (fds 0, 1, 2) if not already open
  // Then exec the init script or shell
  char *sh_argv[] = { "sh", NULL };
  execve("/bin/sh", sh_argv, NULL);
  // If execve returns, something went wrong — fallback
  write(STDERR_FILENO, "[init] failed to exec /bin/sh\n", 30);
  printf("[init] entering idle loop\n");
  for (;;) ; // kernel panic: spin forever
}
