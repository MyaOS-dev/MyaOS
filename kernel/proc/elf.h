#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <myaos/syscall.h>
#include <stdint.h>

typedef int (*elf_entry_t)(int argc, char** argv);

typedef struct {
    void* load_base;
    uint64_t load_size;
    elf_entry_t entry;
    uint64_t vaddr_base;
    uint64_t entry_vaddr;
    uint64_t phdr_vaddr;
    uint16_t phentsize;
    uint16_t phnum;
    char interp[MYAOS_PATH_MAX];
} elf_image_t;

int elf_load_from_vfs(const char* cwd, const char* path, elf_image_t* out);
void elf_unload(elf_image_t* image);

#endif
