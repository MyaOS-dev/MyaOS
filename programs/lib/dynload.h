#ifndef MYAOS_DYNLOAD_H
#define MYAOS_DYNLOAD_H

#include "myaos.h"

#define MYA_DL_FILE_MAX (256u * 1024u)
#define MYA_DL_PT_LOAD 1u
#define MYA_DL_SHT_SYMTAB 2u
#define MYA_DL_SHT_STRTAB 3u
#define MYA_DL_SHT_DYNSYM 11u
#define MYA_DL_PAGE_SIZE 4096ull

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} mya_dl_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} mya_dl_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} mya_dl_shdr_t;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} mya_dl_sym_t;

typedef struct {
    uint8_t* file_buf;
    uint32_t file_size;
    uint8_t* image;
    uint64_t image_size;
    uint64_t min_vaddr;
    const mya_dl_sym_t* symtab;
    uint32_t sym_count;
    const char* strtab;
    uint32_t strtab_size;
} mya_dl_handle_t;

static inline uint64_t mya_dl_align_up(uint64_t value, uint64_t align) {
    return (value + align - 1ull) & ~(align - 1ull);
}

static inline void mya_dl_zero(void* ptr, uint64_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint64_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static inline void mya_dl_copy(void* dst, const void* src, uint64_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (uint64_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static inline void mya_dl_reset(mya_dl_handle_t* h) {
    if (!h) {
        return;
    }
    h->file_buf = NULL;
    h->file_size = 0u;
    h->image = NULL;
    h->image_size = 0u;
    h->min_vaddr = 0u;
    h->symtab = NULL;
    h->sym_count = 0u;
    h->strtab = NULL;
    h->strtab_size = 0u;
}

static inline int mya_dl_close(mya_dl_handle_t* h) {
    if (!h) {
        return -1;
    }
    if (h->image) {
        (void)mya_mem_unmap(h->image);
    }
    if (h->file_buf) {
        (void)mya_mem_unmap(h->file_buf);
    }
    mya_dl_reset(h);
    return 0;
}

static inline int mya_dl_open(const char* path, mya_dl_handle_t* out) {
    mya_dl_ehdr_t* eh;
    mya_dl_phdr_t* ph;
    uint64_t min_vaddr = ~0ull;
    uint64_t max_vaddr = 0ull;
    uint32_t read_size = 0;

    if (!path || !out) {
        return -1;
    }

    mya_dl_reset(out);
    out->file_buf = (uint8_t*)mya_mem_map(MYA_DL_FILE_MAX, MYAOS_MEM_MAP_WRITABLE);
    if (!out->file_buf) {
        return -1;
    }
    if (mya_fs_read(path, out->file_buf, MYA_DL_FILE_MAX, &read_size) != 0 || read_size < sizeof(mya_dl_ehdr_t)) {
        (void)mya_dl_close(out);
        return -1;
    }
    out->file_size = read_size;

    eh = (mya_dl_ehdr_t*)(void*)out->file_buf;
    if (eh->e_ident[0] != 0x7fu || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 2u || eh->e_phentsize != sizeof(mya_dl_phdr_t)) {
        (void)mya_dl_close(out);
        return -1;
    }
    if (eh->e_phoff > out->file_size ||
        (uint64_t)eh->e_phnum * sizeof(mya_dl_phdr_t) > (uint64_t)out->file_size - eh->e_phoff) {
        (void)mya_dl_close(out);
        return -1;
    }

    ph = (mya_dl_phdr_t*)(void*)(out->file_buf + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != MYA_DL_PT_LOAD || ph[i].p_memsz == 0u) {
            continue;
        }
        if (ph[i].p_filesz > ph[i].p_memsz || ph[i].p_offset > out->file_size ||
            ph[i].p_filesz > (uint64_t)out->file_size - ph[i].p_offset) {
            (void)mya_dl_close(out);
            return -1;
        }
        if (ph[i].p_vaddr < min_vaddr) {
            min_vaddr = ph[i].p_vaddr;
        }
        if (ph[i].p_vaddr + ph[i].p_memsz > max_vaddr) {
            max_vaddr = ph[i].p_vaddr + ph[i].p_memsz;
        }
    }

    if (min_vaddr == ~0ull || max_vaddr <= min_vaddr) {
        (void)mya_dl_close(out);
        return -1;
    }

    out->image_size = mya_dl_align_up(max_vaddr - min_vaddr, MYA_DL_PAGE_SIZE);
    out->image = (uint8_t*)mya_mem_map(out->image_size, MYAOS_MEM_MAP_WRITABLE);
    if (!out->image) {
        (void)mya_dl_close(out);
        return -1;
    }
    mya_dl_zero(out->image, out->image_size);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        uint64_t off;
        if (ph[i].p_type != MYA_DL_PT_LOAD || ph[i].p_memsz == 0u) {
            continue;
        }
        off = ph[i].p_vaddr - min_vaddr;
        if (off > out->image_size || ph[i].p_filesz > out->image_size - off) {
            (void)mya_dl_close(out);
            return -1;
        }
        mya_dl_copy(out->image + off, out->file_buf + ph[i].p_offset, ph[i].p_filesz);
    }

    out->min_vaddr = min_vaddr;

    if (eh->e_shoff < out->file_size && eh->e_shentsize == sizeof(mya_dl_shdr_t) && eh->e_shnum > 0u) {
        mya_dl_shdr_t* sh = (mya_dl_shdr_t*)(void*)(out->file_buf + eh->e_shoff);
        uint32_t sym_idx = 0xFFFFFFFFu;

        if ((uint64_t)eh->e_shnum * sizeof(mya_dl_shdr_t) > (uint64_t)out->file_size - eh->e_shoff) {
            (void)mya_dl_close(out);
            return -1;
        }

        for (uint32_t i = 0; i < eh->e_shnum; i++) {
            if (sh[i].sh_type == MYA_DL_SHT_DYNSYM) {
                sym_idx = i;
                break;
            }
        }
        if (sym_idx == 0xFFFFFFFFu) {
            for (uint32_t i = 0; i < eh->e_shnum; i++) {
                if (sh[i].sh_type == MYA_DL_SHT_SYMTAB) {
                    sym_idx = i;
                    break;
                }
            }
        }

        if (sym_idx != 0xFFFFFFFFu) {
            uint32_t str_idx = sh[sym_idx].sh_link;
            if (sh[sym_idx].sh_entsize == sizeof(mya_dl_sym_t) && str_idx < eh->e_shnum &&
                sh[str_idx].sh_type == MYA_DL_SHT_STRTAB &&
                sh[sym_idx].sh_offset <= out->file_size &&
                sh[sym_idx].sh_size <= (uint64_t)out->file_size - sh[sym_idx].sh_offset &&
                sh[str_idx].sh_offset <= out->file_size &&
                sh[str_idx].sh_size <= (uint64_t)out->file_size - sh[str_idx].sh_offset) {
                out->symtab = (const mya_dl_sym_t*)(const void*)(out->file_buf + sh[sym_idx].sh_offset);
                out->sym_count = (uint32_t)(sh[sym_idx].sh_size / sizeof(mya_dl_sym_t));
                out->strtab = (const char*)(const void*)(out->file_buf + sh[str_idx].sh_offset);
                out->strtab_size = (uint32_t)sh[str_idx].sh_size;
            }
        }
    }

    return 0;
}

static inline void* mya_dl_sym(const mya_dl_handle_t* h, const char* symbol_name) {
    if (!h || !h->symtab || !h->strtab || !symbol_name || !symbol_name[0]) {
        return NULL;
    }

    for (uint32_t i = 0; i < h->sym_count; i++) {
        const mya_dl_sym_t* sym = &h->symtab[i];
        const char* cur_name;

        if (sym->st_name == 0u || sym->st_value == 0u || sym->st_name >= h->strtab_size) {
            continue;
        }
        cur_name = h->strtab + sym->st_name;
        if (!mya_streq(cur_name, symbol_name)) {
            continue;
        }
        if (sym->st_value < h->min_vaddr) {
            continue;
        }
        return (void*)(uintptr_t)((uint64_t)(uintptr_t)h->image + (sym->st_value - h->min_vaddr));
    }

    return NULL;
}

#endif
