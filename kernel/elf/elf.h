#ifndef ELF_H
#define ELF_H


typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf_prog_header_t;

#define PT_LOAD 1

#endif // ELF_H
