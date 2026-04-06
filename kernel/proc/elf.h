#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>

typedef int (*elf_entry_t)(int argc, char** argv);

typedef struct {
    void* load_base;
    uint64_t load_size;
    elf_entry_t entry;
} elf_image_t;

int elf_load_from_vfs(const char* cwd, const char* path, elf_image_t* out);
void elf_unload(elf_image_t* image);

#endif
