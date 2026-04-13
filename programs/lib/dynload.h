#ifndef MYAOS_DYNLOAD_H
#define MYAOS_DYNLOAD_H

#include "myaos.h"

#define MYA_DL_FILE_MAX (256u * 1024u)
#define MYA_DL_ET_DYN 3u
#define MYA_DL_EM_X86_64 62u
#define MYA_DL_PT_LOAD 1u
#define MYA_DL_PT_DYNAMIC 2u
#define MYA_DL_SHT_SYMTAB 2u
#define MYA_DL_SHT_STRTAB 3u
#define MYA_DL_SHT_DYNSYM 11u
#define MYA_DL_SHN_UNDEF 0u
#define MYA_DL_SHN_ABS 0xFFF1u
#define MYA_DL_STB_WEAK 2u
#define MYA_DL_DT_NULL 0
#define MYA_DL_DT_PLTRELSZ 2
#define MYA_DL_DT_HASH 4
#define MYA_DL_DT_STRTAB 5
#define MYA_DL_DT_SYMTAB 6
#define MYA_DL_DT_RELA 7
#define MYA_DL_DT_RELASZ 8
#define MYA_DL_DT_RELAENT 9
#define MYA_DL_DT_STRSZ 10
#define MYA_DL_DT_SYMENT 11
#define MYA_DL_DT_INIT 12
#define MYA_DL_DT_FINI 13
#define MYA_DL_DT_PLTREL 20
#define MYA_DL_DT_JMPREL 23
#define MYA_DL_DT_INIT_ARRAY 25
#define MYA_DL_DT_FINI_ARRAY 26
#define MYA_DL_DT_INIT_ARRAYSZ 27
#define MYA_DL_DT_FINI_ARRAYSZ 28
#define MYA_DL_DT_RELA_TYPE 7u
#define MYA_DL_R_X86_64_NONE 0u
#define MYA_DL_R_X86_64_64 1u
#define MYA_DL_R_X86_64_GLOB_DAT 6u
#define MYA_DL_R_X86_64_JUMP_SLOT 7u
#define MYA_DL_R_X86_64_RELATIVE 8u
#define MYA_DL_R_X86_64_32 10u
#define MYA_DL_R_X86_64_32S 11u
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
    int64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    } d_un;
} mya_dl_dyn_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} mya_dl_rela_t;

typedef void* (*mya_dl_resolver_t)(const char* symbol_name, void* user_ctx);

typedef struct {
    mya_dl_resolver_t resolve;
    void* user_ctx;
} mya_dl_open_opts_t;

typedef struct {
    const char* name;
    void* address;
} mya_dl_symbol_t;

typedef struct {
    uint8_t* file_buf;
    uint32_t file_size;
    uint8_t* image;
    uint64_t image_size;
    uint64_t min_vaddr;
    uint64_t max_vaddr;
    uint64_t base_addr;
    const mya_dl_sym_t* symtab;
    uint32_t sym_count;
    const char* strtab;
    uint32_t strtab_size;
    uint8_t linked;
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
    h->max_vaddr = 0u;
    h->base_addr = 0u;
    h->symtab = NULL;
    h->sym_count = 0u;
    h->strtab = NULL;
    h->strtab_size = 0u;
    h->linked = 0u;
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

static inline uint32_t mya_dl_r_type(uint64_t info) {
    return (uint32_t)(info & 0xFFFFFFFFu);
}

static inline uint32_t mya_dl_r_sym(uint64_t info) {
    return (uint32_t)(info >> 32u);
}

static inline uint8_t mya_dl_sym_bind(uint8_t info) {
    return (uint8_t)(info >> 4u);
}

static inline void* mya_dl_vaddr_to_ptr(const mya_dl_handle_t* h, uint64_t vaddr, uint64_t size) {
    uint64_t off;

    if (!h || !h->image || h->max_vaddr <= h->min_vaddr) {
        return NULL;
    }
    if (vaddr < h->min_vaddr) {
        return NULL;
    }
    off = vaddr - h->min_vaddr;
    if (off > h->image_size) {
        return NULL;
    }
    if (size > h->image_size - off) {
        return NULL;
    }
    if (vaddr > h->max_vaddr) {
        return NULL;
    }
    if (size > h->max_vaddr - vaddr) {
        return NULL;
    }
    return h->image + off;
}

static inline int mya_dl_strtab_has_name(const mya_dl_handle_t* h, uint32_t name_off) {
    if (!h || !h->strtab || name_off >= h->strtab_size) {
        return 0;
    }
    for (uint32_t i = name_off; i < h->strtab_size; i++) {
        if (h->strtab[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static inline int mya_dl_defined_symbol_addr(const mya_dl_handle_t* h, const mya_dl_sym_t* sym, uint64_t* out_addr) {
    if (!h || !sym || !out_addr) {
        return -1;
    }
    if (sym->st_shndx == MYA_DL_SHN_UNDEF) {
        return -1;
    }
    if (sym->st_shndx == MYA_DL_SHN_ABS) {
        *out_addr = sym->st_value;
        return 0;
    }
    if (sym->st_value < h->min_vaddr || sym->st_value >= h->max_vaddr) {
        return -1;
    }
    *out_addr = h->base_addr + sym->st_value;
    return 0;
}

static inline int mya_dl_resolve_symbol_value(
    const mya_dl_handle_t* h,
    const mya_dl_open_opts_t* opts,
    uint32_t sym_index,
    uint64_t* out_value
) {
    const mya_dl_sym_t* sym;

    if (!h || !h->symtab || !out_value || sym_index >= h->sym_count) {
        return -1;
    }
    if (sym_index == 0u) {
        *out_value = 0u;
        return 0;
    }
    sym = &h->symtab[sym_index];
    if (mya_dl_defined_symbol_addr(h, sym, out_value) == 0) {
        return 0;
    }

    if (sym->st_shndx != MYA_DL_SHN_UNDEF) {
        return -1;
    }
    if (!mya_dl_strtab_has_name(h, sym->st_name)) {
        return -1;
    }

    if (opts && opts->resolve) {
        void* resolved = opts->resolve(h->strtab + sym->st_name, opts->user_ctx);
        if (resolved) {
            *out_value = (uint64_t)(uintptr_t)resolved;
            return 0;
        }
    }

    if (mya_dl_sym_bind(sym->st_info) == MYA_DL_STB_WEAK) {
        *out_value = 0u;
        return 0;
    }
    return -1;
}

static inline int mya_dl_apply_one_rela(
    mya_dl_handle_t* h,
    const mya_dl_open_opts_t* opts,
    const mya_dl_rela_t* rela
) {
    uint32_t type;
    uint32_t sym_idx;
    uint64_t sym_val = 0u;
    uint8_t* loc;
    int64_t value64;

    if (!h || !rela) {
        return -1;
    }

    type = mya_dl_r_type(rela->r_info);
    sym_idx = mya_dl_r_sym(rela->r_info);

    switch (type) {
    case MYA_DL_R_X86_64_NONE:
        return 0;
    case MYA_DL_R_X86_64_RELATIVE:
        loc = (uint8_t*)mya_dl_vaddr_to_ptr(h, rela->r_offset, sizeof(uint64_t));
        if (!loc) {
            return -1;
        }
        value64 = (int64_t)h->base_addr + rela->r_addend;
        *((uint64_t*)(void*)loc) = (uint64_t)value64;
        return 0;
    case MYA_DL_R_X86_64_GLOB_DAT:
    case MYA_DL_R_X86_64_JUMP_SLOT:
    case MYA_DL_R_X86_64_64:
        loc = (uint8_t*)mya_dl_vaddr_to_ptr(h, rela->r_offset, sizeof(uint64_t));
        if (!loc || mya_dl_resolve_symbol_value(h, opts, sym_idx, &sym_val) != 0) {
            return -1;
        }
        value64 = (int64_t)sym_val + rela->r_addend;
        *((uint64_t*)(void*)loc) = (uint64_t)value64;
        return 0;
    case MYA_DL_R_X86_64_32: {
        uint64_t value;
        loc = (uint8_t*)mya_dl_vaddr_to_ptr(h, rela->r_offset, sizeof(uint32_t));
        if (!loc || mya_dl_resolve_symbol_value(h, opts, sym_idx, &sym_val) != 0) {
            return -1;
        }
        value64 = (int64_t)sym_val + rela->r_addend;
        value = (uint64_t)value64;
        if (value > 0xFFFFFFFFull) {
            return -1;
        }
        *((uint32_t*)(void*)loc) = (uint32_t)value;
        return 0;
    }
    case MYA_DL_R_X86_64_32S: {
        int64_t value;
        loc = (uint8_t*)mya_dl_vaddr_to_ptr(h, rela->r_offset, sizeof(uint32_t));
        if (!loc || mya_dl_resolve_symbol_value(h, opts, sym_idx, &sym_val) != 0) {
            return -1;
        }
        value64 = (int64_t)sym_val + rela->r_addend;
        value = value64;
        if (value < (int64_t)-2147483648ll || value > (int64_t)2147483647ll) {
            return -1;
        }
        *((uint32_t*)(void*)loc) = (uint32_t)(int32_t)value;
        return 0;
    }
    default:
        return -1;
    }
}

static inline int mya_dl_apply_rela_table(
    mya_dl_handle_t* h,
    const mya_dl_open_opts_t* opts,
    uint64_t rela_vaddr,
    uint64_t rela_size,
    uint64_t rela_ent
) {
    const mya_dl_rela_t* rela;
    uint64_t count;

    if (rela_vaddr == 0u || rela_size == 0u) {
        return 0;
    }
    if (rela_ent == 0u) {
        rela_ent = sizeof(mya_dl_rela_t);
    }
    if (rela_ent != sizeof(mya_dl_rela_t) || (rela_size % rela_ent) != 0u) {
        return -1;
    }

    rela = (const mya_dl_rela_t*)mya_dl_vaddr_to_ptr(h, rela_vaddr, rela_size);
    if (!rela) {
        return -1;
    }

    count = rela_size / rela_ent;
    for (uint64_t i = 0u; i < count; i++) {
        if (mya_dl_apply_one_rela(h, opts, &rela[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static inline int mya_dl_call_ifunc(const mya_dl_handle_t* h, uint64_t maybe_addr) {
    void (*fn)(void);
    uint64_t fn_addr = maybe_addr;

    if (!h || maybe_addr == 0u) {
        return 0;
    }
    if (maybe_addr >= h->min_vaddr && maybe_addr < h->max_vaddr) {
        fn_addr = h->base_addr + maybe_addr;
    }
    fn = (void (*)(void))(uintptr_t)fn_addr;
    fn();
    return 0;
}

static inline int mya_dl_run_initializers(
    const mya_dl_handle_t* h,
    uint64_t init_vaddr,
    uint64_t init_array_vaddr,
    uint64_t init_array_size
) {
    const uint64_t* init_array;

    if (!h) {
        return -1;
    }

    if (init_vaddr != 0u) {
        (void)mya_dl_call_ifunc(h, h->base_addr + init_vaddr);
    }

    if (init_array_vaddr == 0u || init_array_size == 0u) {
        return 0;
    }
    if ((init_array_size % sizeof(uint64_t)) != 0u) {
        return -1;
    }

    init_array = (const uint64_t*)mya_dl_vaddr_to_ptr(h, init_array_vaddr, init_array_size);
    if (!init_array) {
        return -1;
    }
    for (uint64_t i = 0u; i < (init_array_size / sizeof(uint64_t)); i++) {
        if (init_array[i] != 0u) {
            (void)mya_dl_call_ifunc(h, init_array[i]);
        }
    }
    return 0;
}

static inline int mya_dl_collect_section_symbols(
    const mya_dl_handle_t* h,
    const mya_dl_ehdr_t* eh,
    const mya_dl_sym_t** out_symtab,
    uint32_t* out_sym_count,
    const char** out_strtab,
    uint32_t* out_strtab_size
) {
    mya_dl_shdr_t* sh;
    uint32_t sym_idx = 0xFFFFFFFFu;

    if (!h || !eh || !out_symtab || !out_sym_count || !out_strtab || !out_strtab_size) {
        return -1;
    }
    if (eh->e_shoff >= h->file_size ||
        eh->e_shentsize != sizeof(mya_dl_shdr_t) ||
        eh->e_shnum == 0u ||
        (uint64_t)eh->e_shnum * sizeof(mya_dl_shdr_t) > (uint64_t)h->file_size - eh->e_shoff) {
        return -1;
    }

    sh = (mya_dl_shdr_t*)(void*)(h->file_buf + eh->e_shoff);
    for (uint32_t i = 0u; i < eh->e_shnum; i++) {
        if (sh[i].sh_type == MYA_DL_SHT_DYNSYM) {
            sym_idx = i;
            break;
        }
    }
    if (sym_idx == 0xFFFFFFFFu) {
        for (uint32_t i = 0u; i < eh->e_shnum; i++) {
            if (sh[i].sh_type == MYA_DL_SHT_SYMTAB) {
                sym_idx = i;
                break;
            }
        }
    }
    if (sym_idx == 0xFFFFFFFFu) {
        return -1;
    }

    {
        uint32_t str_idx = sh[sym_idx].sh_link;
        if (str_idx >= eh->e_shnum ||
            sh[sym_idx].sh_entsize != sizeof(mya_dl_sym_t) ||
            sh[str_idx].sh_type != MYA_DL_SHT_STRTAB ||
            sh[sym_idx].sh_offset > h->file_size ||
            sh[sym_idx].sh_size > (uint64_t)h->file_size - sh[sym_idx].sh_offset ||
            sh[str_idx].sh_offset > h->file_size ||
            sh[str_idx].sh_size > (uint64_t)h->file_size - sh[str_idx].sh_offset) {
            return -1;
        }

        *out_symtab = (const mya_dl_sym_t*)(const void*)(h->file_buf + sh[sym_idx].sh_offset);
        *out_sym_count = (uint32_t)(sh[sym_idx].sh_size / sizeof(mya_dl_sym_t));
        *out_strtab = (const char*)(const void*)(h->file_buf + sh[str_idx].sh_offset);
        *out_strtab_size = (uint32_t)sh[str_idx].sh_size;
    }
    return 0;
}

static inline int mya_dl_open_ex(const char* path, const mya_dl_open_opts_t* opts, mya_dl_handle_t* out) {
    mya_dl_ehdr_t* eh;
    mya_dl_phdr_t* ph;
    uint64_t min_vaddr = ~0ull;
    uint64_t max_vaddr = 0ull;
    uint64_t dyn_symtab = 0u;
    uint64_t dyn_strtab = 0u;
    uint64_t dyn_strsz = 0u;
    uint64_t dyn_hash = 0u;
    uint64_t dyn_rela = 0u;
    uint64_t dyn_relasz = 0u;
    uint64_t dyn_relaent = sizeof(mya_dl_rela_t);
    uint64_t dyn_jmprel = 0u;
    uint64_t dyn_pltrelsz = 0u;
    uint64_t dyn_pltrel = 0u;
    uint64_t dyn_syment = sizeof(mya_dl_sym_t);
    uint64_t dyn_init = 0u;
    uint64_t dyn_init_array = 0u;
    uint64_t dyn_init_arraysz = 0u;
    uint8_t have_dynamic = 0u;
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
        eh->e_ident[4] != 2u || eh->e_ident[5] != 1u ||
        eh->e_machine != MYA_DL_EM_X86_64 || eh->e_type != MYA_DL_ET_DYN ||
        eh->e_phentsize != sizeof(mya_dl_phdr_t)) {
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
    out->max_vaddr = max_vaddr;
    out->base_addr = (uint64_t)(uintptr_t)out->image - min_vaddr;

    for (uint16_t i = 0u; i < eh->e_phnum; i++) {
        if (ph[i].p_type != MYA_DL_PT_DYNAMIC || ph[i].p_memsz < sizeof(mya_dl_dyn_t)) {
            continue;
        }

        {
            const mya_dl_dyn_t* dyn = (const mya_dl_dyn_t*)mya_dl_vaddr_to_ptr(out, ph[i].p_vaddr, ph[i].p_memsz);
            uint64_t dyn_count;
            if (!dyn) {
                (void)mya_dl_close(out);
                return -1;
            }
            have_dynamic = 1u;
            dyn_count = ph[i].p_memsz / sizeof(mya_dl_dyn_t);
            for (uint64_t di = 0u; di < dyn_count; di++) {
                int64_t tag = dyn[di].d_tag;
                uint64_t value = dyn[di].d_un.d_ptr;

                if (tag == MYA_DL_DT_NULL) {
                    break;
                }
                if (tag == MYA_DL_DT_SYMTAB) {
                    dyn_symtab = value;
                } else if (tag == MYA_DL_DT_STRTAB) {
                    dyn_strtab = value;
                } else if (tag == MYA_DL_DT_STRSZ) {
                    dyn_strsz = dyn[di].d_un.d_val;
                } else if (tag == MYA_DL_DT_HASH) {
                    dyn_hash = value;
                } else if (tag == MYA_DL_DT_RELA) {
                    dyn_rela = value;
                } else if (tag == MYA_DL_DT_RELASZ) {
                    dyn_relasz = dyn[di].d_un.d_val;
                } else if (tag == MYA_DL_DT_RELAENT) {
                    dyn_relaent = dyn[di].d_un.d_val;
                } else if (tag == MYA_DL_DT_PLTRELSZ) {
                    dyn_pltrelsz = dyn[di].d_un.d_val;
                } else if (tag == MYA_DL_DT_PLTREL) {
                    dyn_pltrel = dyn[di].d_un.d_val;
                } else if (tag == MYA_DL_DT_JMPREL) {
                    dyn_jmprel = value;
                } else if (tag == MYA_DL_DT_SYMENT) {
                    dyn_syment = dyn[di].d_un.d_val;
                } else if (tag == MYA_DL_DT_INIT) {
                    dyn_init = value;
                } else if (tag == MYA_DL_DT_INIT_ARRAY) {
                    dyn_init_array = value;
                } else if (tag == MYA_DL_DT_INIT_ARRAYSZ) {
                    dyn_init_arraysz = dyn[di].d_un.d_val;
                }
            }
        }
    }

    if (dyn_symtab != 0u && dyn_strtab != 0u &&
        dyn_syment == sizeof(mya_dl_sym_t) &&
        dyn_strsz > 0u && dyn_strsz <= out->image_size) {
        out->symtab = (const mya_dl_sym_t*)mya_dl_vaddr_to_ptr(out, dyn_symtab, sizeof(mya_dl_sym_t));
        out->strtab = (const char*)mya_dl_vaddr_to_ptr(out, dyn_strtab, dyn_strsz);
        out->strtab_size = (uint32_t)dyn_strsz;
        if (!out->symtab || !out->strtab) {
            (void)mya_dl_close(out);
            return -1;
        }

        if (dyn_hash != 0u) {
            const uint32_t* hash = (const uint32_t*)mya_dl_vaddr_to_ptr(out, dyn_hash, 2u * sizeof(uint32_t));
            if (!hash) {
                (void)mya_dl_close(out);
                return -1;
            }
            {
                uint32_t nbucket = hash[0];
                uint32_t nchain = hash[1];
                uint64_t hash_size = (uint64_t)(2u + nbucket + nchain) * sizeof(uint32_t);
                if (!mya_dl_vaddr_to_ptr(out, dyn_hash, hash_size)) {
                    (void)mya_dl_close(out);
                    return -1;
                }
                out->sym_count = nchain;
            }
        }
    }

    if (!out->symtab || !out->strtab || out->sym_count == 0u) {
        const mya_dl_sym_t* sec_sym = NULL;
        uint32_t sec_sym_count = 0u;
        const char* sec_str = NULL;
        uint32_t sec_str_size = 0u;

        if (mya_dl_collect_section_symbols(out, eh, &sec_sym, &sec_sym_count, &sec_str, &sec_str_size) == 0) {
            out->symtab = sec_sym;
            out->sym_count = sec_sym_count;
            out->strtab = sec_str;
            out->strtab_size = sec_str_size;
        }
    }

    if (have_dynamic) {
        if (dyn_relaent == 0u) {
            dyn_relaent = sizeof(mya_dl_rela_t);
        }
        if (dyn_rela != 0u && dyn_relasz != 0u &&
            mya_dl_apply_rela_table(out, opts, dyn_rela, dyn_relasz, dyn_relaent) != 0) {
            (void)mya_dl_close(out);
            return -1;
        }
        if (dyn_jmprel != 0u && dyn_pltrelsz != 0u) {
            if (dyn_pltrel != 0u && dyn_pltrel != MYA_DL_DT_RELA_TYPE) {
                (void)mya_dl_close(out);
                return -1;
            }
            if (mya_dl_apply_rela_table(out, opts, dyn_jmprel, dyn_pltrelsz, dyn_relaent) != 0) {
                (void)mya_dl_close(out);
                return -1;
            }
        }
        if (mya_dl_run_initializers(out, dyn_init, dyn_init_array, dyn_init_arraysz) != 0) {
            (void)mya_dl_close(out);
            return -1;
        }
    }
    out->linked = 1u;
    return 0;
}

static inline int mya_dl_open(const char* path, mya_dl_handle_t* out) {
    return mya_dl_open_ex(path, NULL, out);
}

static inline void* mya_dl_sym(const mya_dl_handle_t* h, const char* symbol_name) {
    uint64_t addr = 0u;

    if (!h || !h->symtab || !h->strtab || !symbol_name || !symbol_name[0]) {
        return NULL;
    }

    for (uint32_t i = 0; i < h->sym_count; i++) {
        const mya_dl_sym_t* sym = &h->symtab[i];
        if (sym->st_name == 0u || !mya_dl_strtab_has_name(h, sym->st_name)) {
            continue;
        }
        if (!mya_streq(h->strtab + sym->st_name, symbol_name)) {
            continue;
        }
        if (mya_dl_defined_symbol_addr(h, sym, &addr) != 0) {
            continue;
        }
        return (void*)(uintptr_t)addr;
    }

    return NULL;
}

static inline void* mya_dl_resolve_from_table(const mya_dl_symbol_t* table, uint32_t count, const char* symbol_name) {
    if (!table || !symbol_name || !symbol_name[0]) {
        return NULL;
    }
    for (uint32_t i = 0u; i < count; i++) {
        if (!table[i].name || !table[i].address) {
            continue;
        }
        if (mya_streq(table[i].name, symbol_name)) {
            return table[i].address;
        }
    }
    return NULL;
}

#endif
