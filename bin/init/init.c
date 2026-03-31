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