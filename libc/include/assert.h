/* No include guard: POSIX requires assert() to be redefined each time
   this header is included, reflecting the current state of NDEBUG. */

#ifndef _ASSERT_FAIL_DECLARED
#define _ASSERT_FAIL_DECLARED
extern void __assert_fail(const char *expr, const char *file, int line, const char *func);
#endif

#undef assert

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif
