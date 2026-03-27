#pragma once

#include "kernel/types.h"

/* ELF program-header flags (p_flags) */
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define PT_LOAD 1

/*
 * ELF64 file header (64 bytes).
 * e_entry / e_phoff / e_shoff are 64-bit in ELF64; the original file had
 * these as uint32_t (ELF32 layout), which truncates addresses on RV64.
 */
typedef struct {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;     /* entry-point VA — must be 64-bit */
    uint64_t      e_phoff;     /* program-header table offset     */
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} elf_header_t;

/*
 * ELF64 program header (56 bytes).
 * In ELF64, p_flags moves to the SECOND field (right after p_type) and all
 * address/size fields become uint64_t.  The original had ELF32 layout where
 * p_flags was second-to-last and everything was uint32_t.
 */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;  /* ELF64: second field, not seventh */
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf_prog_header_t;
