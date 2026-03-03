#ifndef SBUNIX_STDARG_H
#define SBUNIX_STDARG_H

// to help with variable argumaents
typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(d, s)      __builtin_va_copy(d, s)

#endif //SBUNIX_STDARG_H