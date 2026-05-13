/* test stdarg.h, stddef.h, math.h (fp classify), time.h, errno.h, setjmp.h, dirent.h */
#include <stdarg.h>
#include <stddef.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <setjmp.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* stdarg: variadic sum */
static int vsum(int n, ...) {
	va_list ap;
	va_start(ap, n);
	int total = 0;
	for (int i = 0; i < n; i++)
		total += va_arg(ap, int);
	va_end(ap);
	return total;
}

/* stddef: offsetof */
struct Point { int x; int y; };

/* setjmp: non-local jump */
static jmp_buf jbuf;
static int jmp_reached = 0;

static void do_longjmp(void) {
	longjmp(jbuf, 42);
}

int main(void) {
	int fail = 0;

	/* stdarg */
	if (vsum(4, 1, 2, 3, 4) != 10) {
		printf("FAIL stdarg vsum\n");
		fail++;
	}

	/* stddef */
	if (offsetof(struct Point, y) != sizeof(int)) {
		printf("FAIL stddef offsetof\n");
		fail++;
	}
	size_t z = 0;
	ptrdiff_t diff = (ptrdiff_t)10 - (ptrdiff_t)3;
	if (z != 0 || diff != 7) {
		printf("FAIL stddef types\n");
		fail++;
	}

	/* math.h: fpclassify macros (no float arithmetic needed) */
	double zero = 0.0;
	double one  = 1.0;
	if (fpclassify(zero) != FP_ZERO) {
		printf("FAIL math fpclassify zero\n");
		fail++;
	}
	if (fpclassify(one) != FP_NORMAL) {
		printf("FAIL math fpclassify normal\n");
		fail++;
	}

	/* errno.h */
	int fd = open("/no/such/path", 0, 0);
	if (fd >= 0 || errno == 0) {
		printf("FAIL errno not set on bad open\n");
		fail++;
	}
	if (strcmp(strerror(ENOENT), "No such file or directory") != 0) {
		/* some libc use slightly different strings; just check non-empty */
		if (strerror(ENOENT)[0] == '\0') {
			printf("FAIL errno strerror empty\n");
			fail++;
		}
	}

	/* time.h */
	time_t t = time(NULL);
	if (t == (time_t)-1) {
		printf("FAIL time() returned -1\n");
		fail++;
	}

	/* setjmp.h */
	int v = setjmp(jbuf);
	if (v == 0) {
		do_longjmp();
		printf("FAIL setjmp: should not reach here\n");
		fail++;
	} else {
		jmp_reached = (v == 42);
	}
	if (!jmp_reached) {
		printf("FAIL setjmp longjmp value\n");
		fail++;
	}

	/* dirent.h: open /  and read at least one entry */
	DIR *d = opendir("/");
	if (!d) {
		printf("FAIL dirent opendir(/)\n");
		fail++;
	} else {
		struct dirent *ent = readdir(d);
		if (!ent) {
			printf("FAIL dirent readdir empty\n");
			fail++;
		}
		closedir(d);
	}

	if (fail == 0)
		printf("PASS: stdarg stddef math time errno setjmp dirent\n");
	return fail ? 1 : 0;
}
