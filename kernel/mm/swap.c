#include "swap.h"
#include "heap.h"
#include <stddef.h>

#define SWAP_MAX_SLOTS 64u

typedef struct {
    uint8_t used;
    uint8_t reserved0[3];
    uint32_t reserved1;
    uint64_t size;
    uint8_t* data;
} swap_slot_t;

static swap_slot_t g_slots[SWAP_MAX_SLOTS];
static uint64_t g_capacity_bytes;
static uint64_t g_used_bytes;
static uint32_t g_slot_count;
static uint64_t g_swap_out_ops;
static uint64_t g_swap_in_ops;

static void mem_copy(void* dst, const void* src, uint64_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;

    for (uint64_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

void swap_init(uint64_t capacity_bytes, uint32_t max_slots) {
    if (max_slots == 0u || max_slots > SWAP_MAX_SLOTS) {
        max_slots = SWAP_MAX_SLOTS;
    }

    g_capacity_bytes = capacity_bytes;
    g_used_bytes = 0u;
    g_slot_count = max_slots;
    g_swap_out_ops = 0u;
    g_swap_in_ops = 0u;

    for (uint32_t i = 0; i < SWAP_MAX_SLOTS; i++) {
        g_slots[i].used = 0u;
        g_slots[i].size = 0u;
        g_slots[i].data = NULL;
    }
}

int swap_store(const void* data, uint64_t size, uint32_t* out_slot_id) {
    uint32_t slot = SWAP_SLOT_NONE;
    uint8_t* buf;

    if (!data || size == 0u || !out_slot_id) {
        return -1;
    }
    if (size > g_capacity_bytes || g_used_bytes + size < g_used_bytes || g_used_bytes + size > g_capacity_bytes) {
        return -2;
    }

    for (uint32_t i = 0; i < g_slot_count; i++) {
        if (!g_slots[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == SWAP_SLOT_NONE) {
        return -3;
    }

    buf = (uint8_t*)kmalloc((size_t)size);
    if (!buf) {
        return -4;
    }

    mem_copy(buf, data, size);
    g_slots[slot].used = 1u;
    g_slots[slot].size = size;
    g_slots[slot].data = buf;
    g_used_bytes += size;
    g_swap_out_ops++;
    *out_slot_id = slot;
    return 0;
}

int swap_load(uint32_t slot_id, void* out_data, uint64_t out_size) {
    if (slot_id >= g_slot_count || !out_data || !g_slots[slot_id].used) {
        return -1;
    }
    if (out_size < g_slots[slot_id].size) {
        return -1;
    }

    mem_copy(out_data, g_slots[slot_id].data, g_slots[slot_id].size);
    g_swap_in_ops++;
    return 0;
}

int swap_free(uint32_t slot_id) {
    if (slot_id >= g_slot_count || !g_slots[slot_id].used) {
        return -1;
    }

    if (g_slots[slot_id].data) {
        kfree(g_slots[slot_id].data);
    }
    if (g_used_bytes >= g_slots[slot_id].size) {
        g_used_bytes -= g_slots[slot_id].size;
    } else {
        g_used_bytes = 0u;
    }

    g_slots[slot_id].used = 0u;
    g_slots[slot_id].size = 0u;
    g_slots[slot_id].data = NULL;
    return 0;
}

int swap_slot_is_used(uint32_t slot_id) {
    if (slot_id >= g_slot_count) {
        return 0;
    }
    return g_slots[slot_id].used ? 1 : 0;
}

void swap_get_info(myaos_swap_info_t* out) {
    uint32_t used_slots = 0;

    if (!out) {
        return;
    }

    for (uint32_t i = 0; i < g_slot_count; i++) {
        if (g_slots[i].used) {
            used_slots++;
        }
    }

    out->total_bytes = g_capacity_bytes;
    out->used_bytes = g_used_bytes;
    out->slot_count = g_slot_count;
    out->used_slots = used_slots;
    out->swap_out_ops = g_swap_out_ops;
    out->swap_in_ops = g_swap_in_ops;
}
