/* test stdlib.h malloc/free + string.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define N 16

int main(void) {
	char **ptrs = malloc(N * sizeof(char *));
	if (!ptrs) { printf("FAIL alloc array\n"); return 1; }

	for (int i = 0; i < N; i++) {
		ptrs[i] = malloc(32);
		if (!ptrs[i]) { printf("FAIL alloc %d\n", i); return 1; }
		snprintf(ptrs[i], 32, "item-%d", i);
	}
	for (int i = 0; i < N; i++) {
		char expected[32];
		snprintf(expected, 32, "item-%d", i);
		if (strcmp(ptrs[i], expected) != 0) {
			printf("FAIL[%d]: '%s'\n", i, ptrs[i]);
			return 1;
		}
	}
	for (int i = 0; i < N; i++) free(ptrs[i]);
	free(ptrs);
	printf("PASS: %d allocs/frees OK\n", N);
	return 0;
}
