#ifndef SWAP_H
#define SWAP_H

#include <myaos/syscall.h>
#include <stdint.h>

#define SWAP_SLOT_NONE 0xFFFFFFFFu

void swap_init(uint64_t capacity_bytes, uint32_t max_slots);
int swap_store(const void* data, uint64_t size, uint32_t* out_slot_id);
int swap_load(uint32_t slot_id, void* out_data, uint64_t out_size);
int swap_free(uint32_t slot_id);
int swap_slot_is_used(uint32_t slot_id);
void swap_get_info(myaos_swap_info_t* out);

#endif
