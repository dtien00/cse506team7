/* test fcntl.h + sys/stat.h + unistd.h: open/write/read/close */
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int main(void) {
	const char *path = "/tmp/tcc_test_fileio.txt";
	const char *msg = "tcc file I/O test\n";

	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { printf("FAIL open write\n"); return 1; }
	write(fd, msg, strlen(msg));
	close(fd);

	fd = open(path, O_RDONLY, 0);
	if (fd < 0) { printf("FAIL open read\n"); return 1; }
	char buf[64];
	int n = read(fd, buf, sizeof(buf) - 1);
	buf[n] = '\0';
	close(fd);

	if (strcmp(buf, msg) != 0) {
		printf("FAIL content mismatch: '%s'\n", buf);
		return 1;
	}
	unlink(path);
	printf("PASS: read back %d bytes\n", n);
	return 0;
}
