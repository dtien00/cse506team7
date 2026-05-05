#ifndef _INTTYPES_H
#define _INTTYPES_H

#include "stdint.h"

/* imaxdiv_t: returned by imaxdiv() */
typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

/* wchar_t needed for wcstoimax/wcstoumax */
#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
typedef __WCHAR_TYPE__ wchar_t;
#endif

/*
 * PRI* macros — fprintf/printf conversion specifiers.
 *
 * RISC-V LP64 ABI: int=32-bit, long=64-bit.
 *   int_leastN_t  == intN_t          → same specifier as exact
 *   int_fast8_t   == signed char     → "hh"
 *   int_fast16/32_t == int           → ""  (no length modifier)
 *   int_fast64_t  == long            → "l"
 */

/* --- Exact-width PRI* --- */
#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "d"
#define PRId64  "ld"

#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  "i"
#define PRIi64  "li"

#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "u"
#define PRIu64  "lu"

#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "x"
#define PRIx64  "lx"

#define PRIX8   "X"
#define PRIX16  "X"
#define PRIX32  "X"
#define PRIX64  "lX"

#define PRIo8   "o"
#define PRIo16  "o"
#define PRIo32  "o"
#define PRIo64  "lo"

/* --- Least-width PRI* (same underlying types as exact on LP64) --- */
#define PRIdLEAST8   "d"
#define PRIdLEAST16  "d"
#define PRIdLEAST32  "d"
#define PRIdLEAST64  "ld"

#define PRIiLEAST8   "i"
#define PRIiLEAST16  "i"
#define PRIiLEAST32  "i"
#define PRIiLEAST64  "li"

#define PRIuLEAST8   "u"
#define PRIuLEAST16  "u"
#define PRIuLEAST32  "u"
#define PRIuLEAST64  "lu"

#define PRIxLEAST8   "x"
#define PRIxLEAST16  "x"
#define PRIxLEAST32  "x"
#define PRIxLEAST64  "lx"

#define PRIXLEAST8   "X"
#define PRIXLEAST16  "X"
#define PRIXLEAST32  "X"
#define PRIXLEAST64  "lX"

#define PRIoLEAST8   "o"
#define PRIoLEAST16  "o"
#define PRIoLEAST32  "o"
#define PRIoLEAST64  "lo"

/* --- Fast-width PRI* --- */
#define PRIdFAST8    "hhd"
#define PRIdFAST16   "d"
#define PRIdFAST32   "d"
#define PRIdFAST64   "ld"

#define PRIiFAST8    "hhi"
#define PRIiFAST16   "i"
#define PRIiFAST32   "i"
#define PRIiFAST64   "li"

#define PRIuFAST8    "hhu"
#define PRIuFAST16   "u"
#define PRIuFAST32   "u"
#define PRIuFAST64   "lu"

#define PRIxFAST8    "hhx"
#define PRIxFAST16   "x"
#define PRIxFAST32   "x"
#define PRIxFAST64   "lx"

#define PRIXFAST8    "hhX"
#define PRIXFAST16   "X"
#define PRIXFAST32   "X"
#define PRIXFAST64   "lX"

#define PRIoFAST8    "hho"
#define PRIoFAST16   "o"
#define PRIoFAST32   "o"
#define PRIoFAST64   "lo"

/* --- intmax_t / intptr_t PRI* --- */
#define PRIdMAX  "ld"
#define PRIiMAX  "li"
#define PRIuMAX  "lu"
#define PRIxMAX  "lx"
#define PRIXMAX  "lX"
#define PRIoMAX  "lo"

#define PRIdPTR  "ld"
#define PRIiPTR  "li"
#define PRIuPTR  "lu"
#define PRIxPTR  "lx"
#define PRIXPTR  "lX"
#define PRIoPTR  "lo"

/*
 * SCN* macros — fscanf/scanf conversion specifiers.
 */

/* --- Exact-width SCN* --- */
#define SCNd8   "hhd"
#define SCNd16  "hd"
#define SCNd32  "d"
#define SCNd64  "ld"

#define SCNi8   "hhi"
#define SCNi16  "hi"
#define SCNi32  "i"
#define SCNi64  "li"

#define SCNu8   "hhu"
#define SCNu16  "hu"
#define SCNu32  "u"
#define SCNu64  "lu"

#define SCNx8   "hhx"
#define SCNx16  "hx"
#define SCNx32  "x"
#define SCNx64  "lx"

#define SCNo8   "hho"
#define SCNo16  "ho"
#define SCNo32  "o"
#define SCNo64  "lo"

/* --- Least-width SCN* --- */
#define SCNdLEAST8   "hhd"
#define SCNdLEAST16  "hd"
#define SCNdLEAST32  "d"
#define SCNdLEAST64  "ld"

#define SCNiLEAST8   "hhi"
#define SCNiLEAST16  "hi"
#define SCNiLEAST32  "i"
#define SCNiLEAST64  "li"

#define SCNuLEAST8   "hhu"
#define SCNuLEAST16  "hu"
#define SCNuLEAST32  "u"
#define SCNuLEAST64  "lu"

#define SCNxLEAST8   "hhx"
#define SCNxLEAST16  "hx"
#define SCNxLEAST32  "x"
#define SCNxLEAST64  "lx"

#define SCNoLEAST8   "hho"
#define SCNoLEAST16  "ho"
#define SCNoLEAST32  "o"
#define SCNoLEAST64  "lo"

/* --- Fast-width SCN* --- */
#define SCNdFAST8    "hhd"
#define SCNdFAST16   "d"
#define SCNdFAST32   "d"
#define SCNdFAST64   "ld"

#define SCNiFAST8    "hhi"
#define SCNiFAST16   "i"
#define SCNiFAST32   "i"
#define SCNiFAST64   "li"

#define SCNuFAST8    "hhu"
#define SCNuFAST16   "u"
#define SCNuFAST32   "u"
#define SCNuFAST64   "lu"

#define SCNxFAST8    "hhx"
#define SCNxFAST16   "x"
#define SCNxFAST32   "x"
#define SCNxFAST64   "lx"

#define SCNoFAST8    "hho"
#define SCNoFAST16   "o"
#define SCNoFAST32   "o"
#define SCNoFAST64   "lo"

/* --- intmax_t / intptr_t SCN* --- */
#define SCNdMAX  "ld"
#define SCNiMAX  "li"
#define SCNuMAX  "lu"
#define SCNxMAX  "lx"
#define SCNoMAX  "lo"

#define SCNdPTR  "ld"
#define SCNiPTR  "li"
#define SCNuPTR  "lu"
#define SCNxPTR  "lx"
#define SCNoPTR  "lo"

/* Function declarations (CX) */
intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t  strtoimax(const char *restrict nptr, char **restrict endptr, int base);
uintmax_t strtoumax(const char *restrict nptr, char **restrict endptr, int base);
intmax_t  wcstoimax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);
uintmax_t wcstoumax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);

#endif /* _INTTYPES_H */
