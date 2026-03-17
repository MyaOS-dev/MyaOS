#ifndef MYAOS_USER_LIBC_H
#define MYAOS_USER_LIBC_H

#include "myaos.h"

static inline int mya_strcmp(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return (int)((unsigned char)a[i] - (unsigned char)b[i]);
        }
        i++;
    }
    return (int)((unsigned char)a[i] - (unsigned char)b[i]);
}

static inline int mya_starts_with(const char* text, const char* prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static inline int mya_parse_u32(const char* text, uint32_t* out) {
    uint64_t v = 0;
    if (mya_strto_u64(text, &v) != 0 || v > 0xFFFFFFFFu) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static inline void mya_print_kv_u32(const char* key, uint32_t value) {
    mya_puts(key);
    mya_puts("=");
    mya_put_u32(value);
    mya_puts("\n");
}

static inline void mya_print_kv_u64(const char* key, uint64_t value) {
    mya_puts(key);
    mya_puts("=");
    mya_put_u64(value);
    mya_puts("\n");
}

typedef struct {
    uint64_t magic;
    uint64_t payload_size;
} mya_alloc_header_t;

#define MYA_ALLOC_MAGIC 0x4D59414F53414C4CULL

static inline void* mya_malloc(size_t size) {
    const size_t align = 16u;
    const size_t max_size = (size_t)-1;
    size_t aligned_size;
    size_t total_size;
    mya_alloc_header_t* hdr;

    if (size == 0u) {
        return NULL;
    }
    if (size > max_size - (align - 1u)) {
        return NULL;
    }

    aligned_size = (size + (align - 1u)) & ~(align - 1u);
    if (aligned_size > max_size - sizeof(mya_alloc_header_t)) {
        return NULL;
    }
    total_size = aligned_size + sizeof(mya_alloc_header_t);

    hdr = (mya_alloc_header_t*)mya_mem_map(total_size, MYAOS_MEM_MAP_WRITABLE);
    if (!hdr) {
        return NULL;
    }

    hdr->magic = MYA_ALLOC_MAGIC;
    hdr->payload_size = aligned_size;
    return (void*)(hdr + 1);
}

static inline void mya_free(void* ptr) {
    mya_alloc_header_t* hdr;

    if (!ptr) {
        return;
    }

    hdr = ((mya_alloc_header_t*)ptr) - 1;
    if (hdr->magic != MYA_ALLOC_MAGIC) {
        return;
    }

    hdr->magic = 0u;
    hdr->payload_size = 0u;
    (void)mya_mem_unmap(hdr);
}

static inline void* mya_calloc(size_t count, size_t size) {
    uint8_t* out;
    size_t total;

    if (count == 0u || size == 0u) {
        return NULL;
    }
    if (size > ((size_t)-1) / count) {
        return NULL;
    }
    total = count * size;
    out = (uint8_t*)mya_malloc(total);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < total; i++) {
        out[i] = 0;
    }
    return out;
}

#endif
