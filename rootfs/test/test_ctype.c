/* test ctype.h + stdint.h */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
	const char *s = "Hello, World! 123";
	int letters = 0, digits = 0, spaces = 0;
	for (const char *p = s; *p; p++) {
		if (isalpha((unsigned char)*p)) letters++;
		else if (isdigit((unsigned char)*p)) digits++;
		else if (isspace((unsigned char)*p)) spaces++;
	}
	printf("letters=%d digits=%d spaces=%d\n", letters, digits, spaces);
	if (letters != 10 || digits != 3 || spaces != 2) {
		printf("FAIL\n");
		return 1;
	}

	uint32_t x = 0xDEADBEEF;
	uint8_t lo = (uint8_t)(x & 0xFF);
	printf("u32=0x%x lo=0x%x\n", (unsigned)x, (unsigned)lo);
	printf("PASS\n");
	return 0;
}
