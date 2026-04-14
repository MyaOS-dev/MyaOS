#include "module.h"
#include "heap.h"
#include "net.h"
#include "netnic.h"
#include "vfs.h"
#include <stddef.h>
#include <stdint.h>

#define MODULE_ELF_MAX_FILE_SIZE (512u * 1024u)
#define MODULE_ELF_PT_LOAD 1u
#define MODULE_ELF_PT_DYNAMIC 2u
#define MODULE_ELF_ET_DYN 3u
#define MODULE_ELF_EM_X86_64 62u
#define MODULE_ELF_DT_NULL 0
#define MODULE_ELF_DT_RELA 7
#define MODULE_ELF_DT_RELASZ 8
#define MODULE_ELF_DT_RELAENT 9
#define MODULE_ELF_R_X86_64_RELATIVE 8u
#define MODULE_EXT_DIR "/lib/modules/"
#define MODULE_EXT_SUFFIX ".kmod"

typedef struct {
    uint8_t used;
    uint8_t loaded;
    uint8_t optional;
    uint8_t external;
    char name[MYAOS_NAME_MAX];
    char description[MYAOS_DESC_MAX];
    myaos_kmod_hook_t on_load;
    myaos_kmod_hook_t on_unload;
    void* image_base;
    uint64_t image_size;
    char source_path[MYAOS_PATH_MAX];
} module_entry_t;

typedef unsigned char Elf64_Byte;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;
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

typedef struct {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr d_ptr;
    } d_un;
} Elf64_Dyn;

typedef struct {
    Elf64_Addr r_offset;
    Elf64_Xword r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

typedef const myaos_kmod_descriptor_t* (*myaos_kmod_entry_fn_t)(void);

extern const myaos_kmod_descriptor_t __myaos_kmods_start[];
extern const myaos_kmod_descriptor_t __myaos_kmods_end[];

static module_entry_t g_modules[MODULE_MAX_COUNT];
static uint32_t g_module_count;

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0;

    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1u < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t str_len(const char* s) {
    uint32_t n = 0u;
    while (s && s[n]) {
        n++;
    }
    return n;
}

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0;

    if (!a || !b) {
        return 0;
    }

    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

static int str_ends_with(const char* text, const char* suffix) {
    uint32_t text_len = str_len(text);
    uint32_t suffix_len = str_len(suffix);

    if (suffix_len > text_len) {
        return 0;
    }
    for (uint32_t i = 0u; i < suffix_len; i++) {
        if (text[text_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

static void mem_copy(void* dst, const void* src, uint64_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (uint64_t i = 0u; i < size; i++) {
        out[i] = in[i];
    }
}

static void mem_zero(void* dst, uint64_t size) {
    uint8_t* out = (uint8_t*)dst;
    for (uint64_t i = 0u; i < size; i++) {
        out[i] = 0u;
    }
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    if (align == 0u) {
        return value;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static int ptr_in_image(const void* ptr, uint64_t size, const uint8_t* image, uint64_t image_size) {
    uint64_t base;
    uint64_t addr;

    if (!ptr || !image || size == 0u) {
        return 0;
    }

    base = (uint64_t)(uintptr_t)image;
    addr = (uint64_t)(uintptr_t)ptr;
    if (addr < base || addr + size < addr) {
        return 0;
    }
    if (addr + size > base + image_size) {
        return 0;
    }
    return 1;
}

static int cstr_in_image(const char* text, uint32_t max_len, const uint8_t* image, uint64_t image_size) {
    uint64_t base;
    uint64_t addr;
    uint64_t off;

    if (!text || max_len == 0u || !image) {
        return 0;
    }

    base = (uint64_t)(uintptr_t)image;
    addr = (uint64_t)(uintptr_t)text;
    if (addr < base || addr >= base + image_size) {
        return 0;
    }

    off = addr - base;
    for (uint32_t i = 0u; i < max_len; i++) {
        if (off + i >= image_size) {
            return 0;
        }
        if (image[off + i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int is_valid_name(const char* name) {
    uint32_t n = 0;

    if (!name || !name[0]) {
        return 0;
    }

    for (n = 0; name[n]; n++) {
        char c = name[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return 0;
        }
    }

    return n < MYAOS_NAME_MAX;
}

static int find_module_slot(const char* name) {
    if (!name || !name[0]) {
        return -1;
    }

    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        if (!g_modules[i].used) {
            continue;
        }
        if (str_eq(g_modules[i].name, name)) {
            return (int)i;
        }
    }

    return -1;
}

static int extract_name_from_module_path(const char* path, char* out_name, uint32_t out_name_size) {
    const char* name_start;
    uint32_t name_len = 0u;

    if (!path || !out_name || out_name_size == 0u) {
        return -1;
    }
    if (!str_ends_with(path, MODULE_EXT_SUFFIX)) {
        return -1;
    }

    name_start = path;
    for (uint32_t i = 0u; path[i]; i++) {
        if (path[i] == '/') {
            name_start = path + i + 1u;
        }
    }

    while (name_start[name_len] && name_start[name_len] != '.') {
        name_len++;
    }
    if (name_len == 0u || name_len + 1u > out_name_size) {
        return -1;
    }

    for (uint32_t i = 0u; i < name_len; i++) {
        out_name[i] = name_start[i];
    }
    out_name[name_len] = '\0';
    if (!is_valid_name(out_name)) {
        return -1;
    }
    return 0;
}

static int build_module_path_by_name(const char* name, char* out_path, uint32_t out_path_size) {
    uint32_t pos = 0u;
    uint32_t prefix_len = str_len(MODULE_EXT_DIR);
    uint32_t name_len = str_len(name);
    uint32_t suffix_len = str_len(MODULE_EXT_SUFFIX);

    if (!name || !out_path || out_path_size == 0u || !is_valid_name(name)) {
        return -1;
    }
    if (prefix_len + name_len + suffix_len + 1u > out_path_size) {
        return -1;
    }

    for (uint32_t i = 0u; i < prefix_len; i++) {
        out_path[pos++] = MODULE_EXT_DIR[i];
    }
    for (uint32_t i = 0u; i < name_len; i++) {
        out_path[pos++] = name[i];
    }
    for (uint32_t i = 0u; i < suffix_len; i++) {
        out_path[pos++] = MODULE_EXT_SUFFIX[i];
    }
    out_path[pos] = '\0';
    return 0;
}

static int kmod_api_register_netnic_driver(const myaos_kmod_netnic_driver_t* driver) {
    netnic_module_driver_t translated;

    if (!driver || driver->api_version != MYAOS_KMOD_NETNIC_DRIVER_API_VERSION) {
        return -1;
    }

    translated.api_version = NETNIC_MODULE_DRIVER_API_VERSION;
    translated.vendor_id = driver->vendor_id;
    translated.device_id = driver->device_id;
    translated.model = driver->model;
    translated.if_name = driver->if_name;
    translated.driver_name = driver->driver_name;
    translated.init_pci = driver->init_pci;
    translated.ready = driver->ready;
    translated.send_frame = driver->send_frame;
    translated.poll_frame = driver->poll_frame;
    translated.get_mac = driver->get_mac;
    return netnic_register_module_driver(&translated);
}

static int kmod_api_unregister_netnic_driver(const char* driver_name) {
    return netnic_unregister_module_driver(driver_name);
}

static int kmod_api_net_reprobe(void) {
    return net_reprobe();
}

static const myaos_kmod_api_t g_kmod_api = {
    .abi_version = MYAOS_KMOD_ABI_VERSION,
    .register_netnic_driver = kmod_api_register_netnic_driver,
    .unregister_netnic_driver = kmod_api_unregister_netnic_driver,
    .net_reprobe = kmod_api_net_reprobe,
};

static int module_apply_relocations(
    uint8_t* image,
    uint64_t image_size,
    const Elf64_Phdr* phdrs,
    uint16_t phnum,
    uint64_t min_vaddr,
    uint64_t max_vaddr
) {
    const Elf64_Dyn* dyn = NULL;
    uint64_t dyn_count = 0u;
    uint64_t rela_vaddr = 0u;
    uint64_t rela_size = 0u;
    uint64_t rela_ent = sizeof(Elf64_Rela);

    for (uint16_t i = 0u; i < phnum; i++) {
        if (phdrs[i].p_type != MODULE_ELF_PT_DYNAMIC || phdrs[i].p_memsz == 0u) {
            continue;
        }
        if (phdrs[i].p_vaddr < min_vaddr || phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) {
            return -1;
        }
        dyn = (const Elf64_Dyn*)(const void*)(image + (phdrs[i].p_vaddr - min_vaddr));
        dyn_count = phdrs[i].p_memsz / sizeof(Elf64_Dyn);
        break;
    }

    if (!dyn || dyn_count == 0u) {
        return 0;
    }

    for (uint64_t i = 0u; i < dyn_count; i++) {
        if (dyn[i].d_tag == MODULE_ELF_DT_NULL) {
            break;
        }
        if (dyn[i].d_tag == MODULE_ELF_DT_RELA) {
            rela_vaddr = dyn[i].d_un.d_ptr;
        } else if (dyn[i].d_tag == MODULE_ELF_DT_RELASZ) {
            rela_size = dyn[i].d_un.d_val;
        } else if (dyn[i].d_tag == MODULE_ELF_DT_RELAENT) {
            rela_ent = dyn[i].d_un.d_val;
        }
    }

    if (rela_vaddr == 0u || rela_size == 0u) {
        return 0;
    }
    if (rela_ent != sizeof(Elf64_Rela) || rela_vaddr < min_vaddr ||
        rela_vaddr + rela_size < rela_vaddr || rela_vaddr + rela_size > max_vaddr) {
        return -1;
    }

    {
        const uint8_t* rela_bytes = image + (rela_vaddr - min_vaddr);
        uint64_t rela_count = rela_size / sizeof(Elf64_Rela);
        const Elf64_Rela* relas = (const Elf64_Rela*)(const void*)rela_bytes;
        uint64_t image_addr = (uint64_t)(uintptr_t)image;

        for (uint64_t i = 0u; i < rela_count; i++) {
            uint32_t r_type = (uint32_t)(relas[i].r_info & 0xFFFFFFFFu);

            if (r_type != MODULE_ELF_R_X86_64_RELATIVE) {
                return -1;
            }
            if (relas[i].r_offset < min_vaddr || relas[i].r_offset + sizeof(uint64_t) > max_vaddr) {
                return -1;
            }

            {
                uint64_t* patch = (uint64_t*)(void*)(image + (relas[i].r_offset - min_vaddr));
                uint64_t value = image_addr + (uint64_t)relas[i].r_addend;
                *patch = value;
            }
        }
    }

    (void)image_size;
    return 0;
}

static int module_load_elf_image(
    const char* path,
    uint8_t** out_image,
    uint64_t* out_image_size,
    myaos_kmod_entry_fn_t* out_entry
) {
    uint8_t* file_buf;
    uint32_t file_size = 0u;
    Elf64_Ehdr* ehdr;
    Elf64_Phdr* phdrs;
    uint64_t ph_size;
    uint64_t min_vaddr = ~0ull;
    uint64_t max_vaddr = 0ull;
    uint64_t image_size;
    uint8_t* image;

    if (!path || !out_image || !out_image_size || !out_entry) {
        return -1;
    }
    *out_image = NULL;
    *out_image_size = 0u;
    *out_entry = NULL;

    file_buf = (uint8_t*)kmalloc(MODULE_ELF_MAX_FILE_SIZE);
    if (!file_buf) {
        return -1;
    }
    if (vfs_read_file("/", path, file_buf, MODULE_ELF_MAX_FILE_SIZE, &file_size) != 0) {
        kfree(file_buf);
        return -1;
    }
    if (file_size < sizeof(Elf64_Ehdr)) {
        kfree(file_buf);
        return -1;
    }

    ehdr = (Elf64_Ehdr*)(void*)file_buf;
    if (ehdr->e_ident[0] != 0x7Fu || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        kfree(file_buf);
        return -1;
    }
    if (ehdr->e_ident[4] != 2u || ehdr->e_machine != MODULE_ELF_EM_X86_64 || ehdr->e_type != MODULE_ELF_ET_DYN) {
        kfree(file_buf);
        return -1;
    }
    if (ehdr->e_phentsize != sizeof(Elf64_Phdr) || ehdr->e_phnum == 0u) {
        kfree(file_buf);
        return -1;
    }

    ph_size = (uint64_t)ehdr->e_phnum * (uint64_t)ehdr->e_phentsize;
    if (ehdr->e_phoff > file_size || ph_size > (uint64_t)file_size - ehdr->e_phoff) {
        kfree(file_buf);
        return -1;
    }
    phdrs = (Elf64_Phdr*)(void*)(file_buf + ehdr->e_phoff);

    for (uint16_t i = 0u; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != MODULE_ELF_PT_LOAD || phdrs[i].p_memsz == 0u) {
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

    if (min_vaddr == ~0ull || max_vaddr <= min_vaddr ||
        ehdr->e_entry < min_vaddr || ehdr->e_entry >= max_vaddr) {
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

    for (uint16_t i = 0u; i < ehdr->e_phnum; i++) {
        uint64_t off;
        if (phdrs[i].p_type != MODULE_ELF_PT_LOAD || phdrs[i].p_memsz == 0u) {
            continue;
        }
        off = phdrs[i].p_vaddr - min_vaddr;
        if (off > image_size || phdrs[i].p_memsz > image_size - off) {
            kfree(image);
            kfree(file_buf);
            return -1;
        }
        mem_copy(image + off, file_buf + phdrs[i].p_offset, phdrs[i].p_filesz);
    }

    if (module_apply_relocations(image, image_size, phdrs, ehdr->e_phnum, min_vaddr, max_vaddr) != 0) {
        kfree(image);
        kfree(file_buf);
        return -1;
    }

    *out_image = image;
    *out_image_size = image_size;
    *out_entry = (myaos_kmod_entry_fn_t)(uintptr_t)(image + (ehdr->e_entry - min_vaddr));
    kfree(file_buf);
    return 0;
}

static int module_register_from_file(const char* path, const char* expected_name, uint8_t force_load) {
    uint8_t* image = NULL;
    uint64_t image_size = 0u;
    myaos_kmod_entry_fn_t entry = NULL;
    const myaos_kmod_descriptor_t* desc;
    int slot;
    int rc;

    if (!path || path[0] != '/') {
        return -1;
    }
    if (!str_ends_with(path, MODULE_EXT_SUFFIX)) {
        return -1;
    }

    if (module_load_elf_image(path, &image, &image_size, &entry) != 0) {
        return -1;
    }
    if (!entry) {
        kfree(image);
        return -1;
    }

    desc = entry();
    if (!desc || !ptr_in_image(desc, sizeof(*desc), image, image_size)) {
        kfree(image);
        return -1;
    }
    if (desc->abi_version != MYAOS_KMOD_ABI_VERSION ||
        !desc->name || !cstr_in_image(desc->name, MYAOS_NAME_MAX, image, image_size) ||
        !is_valid_name(desc->name) ||
        (expected_name && !str_eq(desc->name, expected_name))) {
        kfree(image);
        return -1;
    }
    if (desc->description && !cstr_in_image(desc->description, MYAOS_DESC_MAX, image, image_size)) {
        kfree(image);
        return -1;
    }
    if ((desc->on_load && !ptr_in_image((const void*)(uintptr_t)desc->on_load, 1u, image, image_size)) ||
        (desc->on_unload && !ptr_in_image((const void*)(uintptr_t)desc->on_unload, 1u, image, image_size))) {
        kfree(image);
        return -1;
    }

    slot = find_module_slot(desc->name);
    if (slot >= 0) {
        kfree(image);
        return force_load ? module_load(desc->name) : 0;
    }

    rc = module_register_ops(
        desc->name,
        desc->description ? desc->description : "",
        desc->optional,
        desc->autoload,
        desc->on_load,
        desc->on_unload
    );
    if (rc != 0) {
        kfree(image);
        return rc;
    }

    slot = find_module_slot(desc->name);
    if (slot < 0) {
        kfree(image);
        return -1;
    }

    g_modules[slot].external = 1u;
    g_modules[slot].image_base = image;
    g_modules[slot].image_size = image_size;
    str_copy(g_modules[slot].source_path, path, sizeof(g_modules[slot].source_path));

    if (force_load && !g_modules[slot].loaded) {
        rc = module_load(desc->name);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

static int module_load_by_name_fallback(const char* name) {
    char path[MYAOS_PATH_MAX];

    if (build_module_path_by_name(name, path, sizeof(path)) != 0) {
        return -1;
    }
    return module_register_from_file(path, name, 1u);
}

void module_init(void) {
    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        g_modules[i].used = 0u;
        g_modules[i].loaded = 0u;
        g_modules[i].optional = 0u;
        g_modules[i].external = 0u;
        g_modules[i].name[0] = '\0';
        g_modules[i].description[0] = '\0';
        g_modules[i].on_load = NULL;
        g_modules[i].on_unload = NULL;
        g_modules[i].image_base = NULL;
        g_modules[i].image_size = 0u;
        g_modules[i].source_path[0] = '\0';
    }
    g_module_count = 0u;
}

int module_register_ops(
    const char* name,
    const char* description,
    uint8_t optional,
    uint8_t autoload,
    myaos_kmod_hook_t on_load,
    myaos_kmod_hook_t on_unload
) {
    int slot;

    if (!is_valid_name(name)) {
        return -1;
    }

    slot = find_module_slot(name);
    if (slot < 0) {
        if (g_module_count >= MODULE_MAX_COUNT) {
            return -1;
        }

        for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
            if (!g_modules[i].used) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) {
            return -1;
        }

        g_modules[slot].used = 1u;
        g_modules[slot].loaded = 0u;
        g_modules[slot].optional = 0u;
        g_modules[slot].external = 0u;
        g_modules[slot].image_base = NULL;
        g_modules[slot].image_size = 0u;
        g_modules[slot].source_path[0] = '\0';
        g_module_count++;
    }

    str_copy(g_modules[slot].name, name, sizeof(g_modules[slot].name));
    str_copy(g_modules[slot].description, description ? description : "", sizeof(g_modules[slot].description));
    g_modules[slot].optional = optional ? 1u : 0u;
    g_modules[slot].on_load = on_load;
    g_modules[slot].on_unload = on_unload;

    if (autoload && !g_modules[slot].loaded) {
        return module_load(name);
    }

    return 0;
}

int module_register(const char* name, const char* description, uint8_t optional, uint8_t autoload) {
    return module_register_ops(name, description, optional, autoload, NULL, NULL);
}

int module_register_kmod(const myaos_kmod_descriptor_t* desc) {
    if (!desc || desc->abi_version != MYAOS_KMOD_ABI_VERSION) {
        return -1;
    }
    return module_register_ops(
        desc->name,
        desc->description ? desc->description : "",
        desc->optional,
        desc->autoload,
        desc->on_load,
        desc->on_unload
    );
}

static void module_register_embedded(void) {
    const myaos_kmod_descriptor_t* it = __myaos_kmods_start;

    while (it < __myaos_kmods_end) {
        (void)module_register_kmod(it);
        it++;
    }
}

void module_register_defaults(void) {
    (void)module_register("core", "core kernel services", 0u, 1u);
    (void)module_register("net", "network stack and sockets", 0u, 1u);
    (void)module_register("swap", "explicit swap subsystem", 1u, 1u);
    (void)module_register("hotplug", "runtime ramdisk hot-plug", 1u, 1u);
    (void)module_register("ldso", "user-space dynamic loader support", 1u, 1u);
    (void)module_register("posix", "POSIX compatibility layer", 1u, 1u);
    module_register_embedded();
}

int module_load(const char* name) {
    int slot = find_module_slot(name);

    if (slot < 0) {
        if (module_load_by_name_fallback(name) != 0) {
            return -1;
        }
        slot = find_module_slot(name);
        if (slot < 0) {
            return -1;
        }
    }
    if (g_modules[slot].loaded) {
        return 0;
    }

    if (g_modules[slot].on_load) {
        int rc = g_modules[slot].on_load(&g_kmod_api);
        if (rc != 0) {
            return rc;
        }
    }

    g_modules[slot].loaded = 1u;
    return 0;
}

int module_load_file(const char* path) {
    char name[MYAOS_NAME_MAX];
    int slot;

    if (!path || path[0] != '/') {
        return -1;
    }

    if (extract_name_from_module_path(path, name, sizeof(name)) == 0) {
        slot = find_module_slot(name);
        if (slot >= 0) {
            return module_load(name);
        }
    }

    return module_register_from_file(path, NULL, 1u);
}

int module_unload(const char* name) {
    int slot = find_module_slot(name);

    if (slot < 0) {
        return -1;
    }
    if (!g_modules[slot].optional) {
        return -2;
    }
    if (!g_modules[slot].loaded) {
        return 0;
    }

    if (g_modules[slot].on_unload) {
        int rc = g_modules[slot].on_unload(&g_kmod_api);
        if (rc != 0) {
            return rc;
        }
    }

    g_modules[slot].loaded = 0u;
    return 0;
}

int module_set_loaded(const char* name, uint8_t loaded) {
    return loaded ? module_load(name) : module_unload(name);
}

int module_is_loaded(const char* name) {
    int slot = find_module_slot(name);

    if (slot < 0) {
        return 0;
    }

    return g_modules[slot].loaded ? 1 : 0;
}

uint32_t module_count(void) {
    return g_module_count;
}

uint32_t module_loaded_count(void) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        if (g_modules[i].used && g_modules[i].loaded) {
            count++;
        }
    }

    return count;
}

int module_list(myaos_module_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t count = 0;

    if (!out_count) {
        return -1;
    }

    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        if (!g_modules[i].used) {
            continue;
        }

        if (count < max_entries && out) {
            str_copy(out[count].name, g_modules[i].name, sizeof(out[count].name));
            str_copy(out[count].description, g_modules[i].description, sizeof(out[count].description));
            out[count].loaded = g_modules[i].loaded;
            out[count].optional = g_modules[i].optional;
            out[count].reserved0 = 0u;
        }
        count++;
    }

    if (count > max_entries) {
        count = max_entries;
    }
    *out_count = count;
    return 0;
}
