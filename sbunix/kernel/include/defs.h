#ifndef SBUNIX_DEFS_H
#define SBUNIX_DEFS_H

#define NULL ((void*)0)
#define UINT64_MAX  (~0ULL)

typedef unsigned long  uint64_t;
typedef          long   int64_t;
typedef unsigned int   uint32_t;
typedef          int    int32_t;
typedef unsigned short uint16_t;
typedef          short  int16_t;
typedef unsigned char   uint8_t;
typedef          char    int8_t;

typedef uint64_t size_t;

#endif //SBUNIX_DEFS_H