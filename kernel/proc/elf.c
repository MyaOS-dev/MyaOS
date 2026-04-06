#include "elf.h"
#include "heap.h"
#include "vfs.h"
#include <stdint.h>

#define ELF_MAX_FILE_SIZE (256u * 1024u)
#define PT_LOAD 1u
#define ET_EXEC 2u
#define ET_DYN 3u
#define EM_X86_64 62u

typedef unsigned char Elf64_Byte;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

typedef struct {
    Elf64_Byte e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

static uint64_t align_up(uint64_t value, uint64_t align) {
    if (align == 0) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static void mem_zero(void* dst, uint64_t size) {
    uint8_t* out = (uint8_t*)dst;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = 0;
    }
}

static void mem_copy(void* dst, const void* src, uint64_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

int elf_load_from_vfs(const char* cwd, const char* path, elf_image_t* out) {
    uint8_t* file_buf;
    uint32_t file_size = 0;
    Elf64_Ehdr* ehdr;
    Elf64_Phdr* phdrs;
    uint64_t ph_table_size;
    uint64_t min_vaddr = ~0ULL;
    uint64_t max_vaddr = 0;
    uint8_t* image;
    uint64_t image_size;

    if (!out) {
        return -1;
    }

    out->load_base = NULL;
    out->load_size = 0;
    out->entry = NULL;

    file_buf = (uint8_t*)kmalloc(ELF_MAX_FILE_SIZE);
    if (!file_buf) {
        return -1;
    }

    if (vfs_read_file(cwd, path, file_buf, ELF_MAX_FILE_SIZE, &file_size) != 0) {
        kfree(file_buf);
        return -1;
    }
    if (file_size < sizeof(Elf64_Ehdr)) {
        kfree(file_buf);
        return -1;
    }

    ehdr = (Elf64_Ehdr*)(void*)file_buf;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        kfree(file_buf);
        return -1;
    }
    if (ehdr->e_ident[4] != 2 || ehdr->e_machine != EM_X86_64) {
        kfree(file_buf);
        return -1;
    }
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        kfree(file_buf);
        return -1;
    }
    if (ehdr->e_phentsize != sizeof(Elf64_Phdr)) {
        kfree(file_buf);
        return -1;
    }
    ph_table_size = (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    if (ehdr->e_phoff > file_size || ph_table_size > (uint64_t)file_size - ehdr->e_phoff) {
        kfree(file_buf);
        return -1;
    }

    phdrs = (Elf64_Phdr*)(void*)(file_buf + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) {
            continue;
        }
        if (phdrs[i].p_memsz == 0) {
            continue;
        }
        if (phdrs[i].p_filesz > phdrs[i].p_memsz) {
            kfree(file_buf);
            return -1;
        }
        if (phdrs[i].p_offset > file_size || phdrs[i].p_filesz > (uint64_t)file_size - phdrs[i].p_offset) {
            kfree(file_buf);
            return -1;
        }
        if (phdrs[i].p_vaddr + phdrs[i].p_memsz < phdrs[i].p_vaddr) {
            kfree(file_buf);
            return -1;
        }
        if (phdrs[i].p_vaddr < min_vaddr) {
            min_vaddr = phdrs[i].p_vaddr;
        }
        if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) {
            max_vaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        }
    }

    if (min_vaddr == ~0ULL || max_vaddr <= min_vaddr) {
        kfree(file_buf);
        return -1;
    }
    if (ehdr->e_entry < min_vaddr || ehdr->e_entry >= max_vaddr) {
        kfree(file_buf);
        return -1;
    }

    image_size = align_up(max_vaddr - min_vaddr, 16u);
    image = (uint8_t*)kmalloc((size_t)image_size);
    if (!image) {
        kfree(file_buf);
        return -1;
    }
    mem_zero(image, image_size);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        uint64_t offset;
        if (phdrs[i].p_type != PT_LOAD || phdrs[i].p_memsz == 0) {
            continue;
        }
        offset = phdrs[i].p_vaddr - min_vaddr;
        if (offset > image_size || phdrs[i].p_memsz > image_size - offset) {
            kfree(image);
            kfree(file_buf);
            return -1;
        }
        mem_copy(image + offset, file_buf + phdrs[i].p_offset, phdrs[i].p_filesz);
    }

    out->load_base = image;
    out->load_size = image_size;
    out->entry = (elf_entry_t)(uintptr_t)(image + (ehdr->e_entry - min_vaddr));

    kfree(file_buf);
    return 0;
}

void elf_unload(elf_image_t* image) {
    if (!image) {
        return;
    }

    if (image->load_base) {
        kfree(image->load_base);
    }

    image->load_base = NULL;
    image->load_size = 0;
    image->entry = NULL;
}
