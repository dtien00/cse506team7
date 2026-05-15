/* test stdio.h + stdlib.h + string.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
	char buf[64];
	int n = snprintf(buf, sizeof(buf), "hello %s %d", "world", 42);
	if (n != 14) {
		printf("FAIL snprintf: got %d\n", n);
		return 1;
	}
	if (strcmp(buf, "hello world 42") != 0) {
		printf("FAIL strcmp: got '%s'\n", buf);
		return 1;
	}
	size_t len = strlen(buf);
	printf("PASS: '%s' len=%zu\n", buf, len);
	return 0;
}
