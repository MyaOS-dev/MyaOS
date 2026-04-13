#include "scheduler_internal.h"

int scheduler_shm_create(const char* name, uint32_t size, int32_t* out_id) {
    int slot;

    if (!name || !name[0] || !out_id || size == 0u || size > SCHED_SHM_DATA_MAX) {
        return -1;
    }

    slot = find_shm_slot_by_name(name);
    if (slot >= 0) {
        if (size > g_shm[slot].size) {
            return -1;
        }
        *out_id = slot + 1;
        return 0;
    }

    for (uint32_t i = 0; i < SCHED_MAX_SHM_SEGMENTS; i++) {
        if (g_shm[i].used) {
            continue;
        }
        g_shm[i].used = 1u;
        g_shm[i].owner_uid = scheduler_current_uid();
        g_shm[i].size = size;
        str_copy(g_shm[i].name, name, sizeof(g_shm[i].name));
        mem_zero(g_shm[i].data, sizeof(g_shm[i].data));
        *out_id = (int32_t)i + 1;
        return 0;
    }
    return -1;
}

int scheduler_shm_open(const char* name, int32_t* out_id) {
    int slot;

    if (!name || !name[0] || !out_id) {
        return -1;
    }
    slot = find_shm_slot_by_name(name);
    if (slot < 0) {
        return -1;
    }
    *out_id = slot + 1;
    return 0;
}

int scheduler_shm_read(int32_t id, uint32_t offset, void* out_buf, uint32_t max_len, uint32_t* out_len) {
    sched_shm_t* shm = shm_from_id(id);
    uint32_t copy_len;

    if (!shm || !out_len || (max_len != 0u && !out_buf)) {
        return -1;
    }
    if (offset >= shm->size || max_len == 0u) {
        *out_len = 0u;
        return 0;
    }

    copy_len = shm->size - offset;
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    mem_copy(out_buf, &shm->data[offset], copy_len);
    *out_len = copy_len;
    return 0;
}

int scheduler_shm_write(int32_t id, uint32_t offset, const void* data, uint32_t len, uint32_t* out_written) {
    sched_shm_t* shm = shm_from_id(id);
    uint32_t copy_len;
    uint32_t uid = scheduler_current_uid();

    if (!shm || !out_written || (len != 0u && !data)) {
        return -1;
    }
    if (uid != 0u && uid != shm->owner_uid) {
        return -1;
    }
    if (offset >= shm->size || len == 0u) {
        *out_written = 0u;
        return 0;
    }

    copy_len = shm->size - offset;
    if (copy_len > len) {
        copy_len = len;
    }
    mem_copy(&shm->data[offset], data, copy_len);
    *out_written = copy_len;
    return 0;
}

int scheduler_shm_close(int32_t id) {
    return shm_from_id(id) ? 0 : -1;
}

int scheduler_stdout_redirect_info(char* out_path, size_t out_size, uint8_t* out_append, uint8_t* out_truncate_now) {
    sched_proc_t* proc = current_proc();

    if (!proc || !proc->stdout_redirect) {
        return 0;
    }
    if (out_path && out_size > 0) {
        str_copy(out_path, proc->stdout_path, out_size);
    }
    if (out_append) {
        *out_append = proc->stdout_append;
    }
    if (out_truncate_now) {
        *out_truncate_now = (uint8_t)((proc->stdout_append == 0u && proc->stdout_truncated == 0u) ? 1u : 0u);
    }
    return 1;
}

void scheduler_stdout_redirect_mark_truncated(void) {
    sched_proc_t* proc = current_proc();
    if (!proc) {
        return;
    }
    proc->stdout_truncated = 1;
}

int scheduler_mem_map_current(uint64_t size, uint8_t writable, uint64_t* out_addr) {
    sched_proc_t* proc = current_proc();
    uint64_t map_window_start;
    uint64_t map_window_end;
    uint64_t image_end;
    uint64_t alloc_size;
    uint64_t candidate;
    uint64_t prev_cr3;
    int free_entry = -1;
    int found = 0;

    if (!proc || !proc->user_mode || !out_addr || proc->user_region_base == 0u ||
        proc->user_region_end <= proc->user_region_base) {
        return -1;
    }
    if (size == 0u) {
        return -1;
    }

    alloc_size = align_up(size, PAGE_SIZE);
    if (alloc_size == 0u || alloc_size < size) {
        return -1;
    }
    if (proc->vm_limit_bytes != 0u) {
        uint64_t mapped = proc_user_mapped_bytes(proc);
        if (mapped > proc->vm_limit_bytes || alloc_size > (proc->vm_limit_bytes - mapped)) {
            return -1;
        }
    }

    map_window_start = proc->user_region_base + USER_MMAP_MIN_OFFSET;
    if (proc->user_argv_base != 0u) {
        map_window_end = proc->user_argv_base;
    } else {
        /* Linux compat keeps argv/env on the initial stack and does not reserve
           a dedicated argv mapping region, so mmap must stop before stack pages. */
        map_window_end = proc->user_stack_base;
    }
    if (map_window_end <= map_window_start) {
        return -1;
    }

    image_end = align_up(proc->user_image_base + proc->user_image_size, PAGE_SIZE);
    if (image_end > map_window_start) {
        map_window_start = image_end;
    }
    if (map_window_start >= map_window_end || alloc_size > (map_window_end - map_window_start)) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            free_entry = (int)i;
            break;
        }
    }
    if (free_entry < 0) {
        return -1;
    }

    candidate = proc->user_mmap_next;
    if (candidate < map_window_start || candidate >= map_window_end) {
        candidate = map_window_start;
    }
    candidate = align_up(candidate, PAGE_SIZE);

    while (candidate + alloc_size > candidate && candidate + alloc_size <= map_window_end) {
        uint64_t candidate_end = candidate + alloc_size;
        uint64_t next_candidate = 0u;
        uint8_t conflict = 0u;

        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            uint64_t map_base;
            uint64_t map_end;

            if (!proc->user_maps[i].used) {
                continue;
            }
            map_base = proc->user_maps[i].base;
            map_end = map_base + proc->user_maps[i].size;
            if (map_end <= map_base) {
                continue;
            }
            if (!ranges_overlap(candidate, candidate_end, map_base, map_end)) {
                continue;
            }
            conflict = 1u;
            if (map_end > next_candidate) {
                next_candidate = map_end;
            }
        }

        if (!conflict) {
            found = 1;
            break;
        }
        if (next_candidate <= candidate) {
            break;
        }
        candidate = align_up(next_candidate, PAGE_SIZE);
    }

    if (!found) {
        return -1;
    }

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return -1;
    }
    if (paging_alloc_user_range(candidate, alloc_size, writable ? 1u : 0u) != 0) {
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    (void)paging_switch_to(prev_cr3);

    proc->user_maps[free_entry].used = 1u;
    proc->user_maps[free_entry].writable = writable ? 1u : 0u;
    proc->user_maps[free_entry].reserved0 = 0u;
    proc->user_maps[free_entry].reserved1 = SWAP_SLOT_NONE;
    proc->user_maps[free_entry].base = candidate;
    proc->user_maps[free_entry].size = alloc_size;
    proc->user_mmap_next = candidate + alloc_size;
    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        int peer_slot = -1;

        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used) {
                peer_slot = (int)i;
                break;
            }
        }
        if (peer_slot >= 0) {
            peer->user_maps[peer_slot].used = 1u;
            peer->user_maps[peer_slot].writable = writable ? 1u : 0u;
            peer->user_maps[peer_slot].reserved0 = 0u;
            peer->user_maps[peer_slot].reserved1 = SWAP_SLOT_NONE;
            peer->user_maps[peer_slot].base = candidate;
            peer->user_maps[peer_slot].size = alloc_size;
        }
        if (peer->user_mmap_next == 0u || peer->user_mmap_next < candidate + alloc_size) {
            peer->user_mmap_next = candidate + alloc_size;
        }
    }
    *out_addr = candidate;
    return 0;
}

int scheduler_mem_unmap_current(uint64_t addr) {
    sched_proc_t* proc = current_proc();
    uint64_t prev_cr3;
    uint64_t map_base = 0;
    uint64_t map_size = 0;
    int map_idx = -1;

    if (!proc || !proc->user_mode || addr == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            continue;
        }
        if (proc->user_maps[i].base == addr) {
            map_idx = (int)i;
            break;
        }
    }
    if (map_idx < 0) {
        return -1;
    }

    map_base = proc->user_maps[map_idx].base;
    map_size = proc->user_maps[map_idx].size;
    if (map_size == 0u) {
        return -1;
    }

    if (user_map_is_swapped(&proc->user_maps[map_idx])) {
        (void)swap_free(proc->user_maps[map_idx].reserved1);
    } else {
        prev_cr3 = paging_current_cr3();
        if (paging_switch_to(proc->page_table_cr3) != 0) {
            return -1;
        }
        paging_free_user_range(map_base, map_size);
        (void)paging_switch_to(prev_cr3);
    }

    proc->user_maps[map_idx].used = 0u;
    proc->user_maps[map_idx].writable = 0u;
    proc->user_maps[map_idx].reserved0 = 0u;
    proc->user_maps[map_idx].reserved1 = SWAP_SLOT_NONE;
    proc->user_maps[map_idx].base = 0u;
    proc->user_maps[map_idx].size = 0u;
    if (proc->user_mmap_next == 0u || map_base < proc->user_mmap_next) {
        proc->user_mmap_next = map_base;
    }
    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used || peer->user_maps[i].base != map_base) {
                continue;
            }
            peer->user_maps[i].used = 0u;
            peer->user_maps[i].writable = 0u;
            peer->user_maps[i].reserved0 = 0u;
            peer->user_maps[i].reserved1 = SWAP_SLOT_NONE;
            peer->user_maps[i].base = 0u;
            peer->user_maps[i].size = 0u;
            break;
        }
        if (peer->user_mmap_next == 0u || map_base < peer->user_mmap_next) {
            peer->user_mmap_next = map_base;
        }
    }
    return 0;
}

int scheduler_mem_swap_out_current(uint64_t addr) {
    sched_proc_t* proc = current_proc();
    uint64_t prev_cr3;
    uint64_t map_base = 0;
    uint64_t map_size = 0;
    uint32_t slot_id = SWAP_SLOT_NONE;
    int map_idx = -1;

    if (!proc || !proc->user_mode || addr == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            continue;
        }
        if (proc->user_maps[i].base == addr) {
            map_idx = (int)i;
            break;
        }
    }
    if (map_idx < 0 || proc->user_maps[map_idx].size == 0u) {
        return -1;
    }
    if (user_map_is_swapped(&proc->user_maps[map_idx])) {
        return -2;
    }

    map_base = proc->user_maps[map_idx].base;
    map_size = proc->user_maps[map_idx].size;

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return -1;
    }
    if (swap_store((const void*)(uintptr_t)map_base, map_size, &slot_id) != 0) {
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    paging_free_user_range(map_base, map_size);
    (void)paging_switch_to(prev_cr3);

    proc->user_maps[map_idx].reserved0 |= USER_MAP_FLAG_SWAPPED;
    proc->user_maps[map_idx].reserved1 = slot_id;

    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used || peer->user_maps[i].base != map_base) {
                continue;
            }
            peer->user_maps[i].reserved0 |= USER_MAP_FLAG_SWAPPED;
            peer->user_maps[i].reserved1 = slot_id;
            break;
        }
    }

    return 0;
}

int scheduler_mem_swap_in_current(uint64_t addr) {
    sched_proc_t* proc = current_proc();
    uint64_t prev_cr3;
    uint64_t map_base = 0;
    uint64_t map_size = 0;
    uint32_t slot_id = SWAP_SLOT_NONE;
    uint8_t writable = 0u;
    int map_idx = -1;

    if (!proc || !proc->user_mode || addr == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
        if (!proc->user_maps[i].used) {
            continue;
        }
        if (proc->user_maps[i].base == addr) {
            map_idx = (int)i;
            break;
        }
    }
    if (map_idx < 0 || proc->user_maps[map_idx].size == 0u) {
        return -1;
    }
    if (!user_map_is_swapped(&proc->user_maps[map_idx])) {
        return -2;
    }

    map_base = proc->user_maps[map_idx].base;
    map_size = proc->user_maps[map_idx].size;
    slot_id = proc->user_maps[map_idx].reserved1;
    writable = proc->user_maps[map_idx].writable ? 1u : 0u;

    if (!swap_slot_is_used(slot_id)) {
        return -1;
    }

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(proc->page_table_cr3) != 0) {
        return -1;
    }
    if (paging_alloc_user_range(map_base, map_size, writable) != 0) {
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    if (swap_load(slot_id, (void*)(uintptr_t)map_base, map_size) != 0) {
        paging_free_user_range(map_base, map_size);
        (void)paging_switch_to(prev_cr3);
        return -1;
    }
    (void)paging_switch_to(prev_cr3);
    (void)swap_free(slot_id);

    proc->user_maps[map_idx].reserved0 &= (uint16_t)~USER_MAP_FLAG_SWAPPED;
    proc->user_maps[map_idx].reserved1 = SWAP_SLOT_NONE;

    for (uint32_t p = 0; p < SCHED_MAX_PROCS; p++) {
        sched_proc_t* peer = &g_procs[p];
        if (!peer->used || peer == proc || !peer->user_mode || peer->page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
            if (!peer->user_maps[i].used || peer->user_maps[i].base != map_base) {
                continue;
            }
            peer->user_maps[i].reserved0 &= (uint16_t)~USER_MAP_FLAG_SWAPPED;
            peer->user_maps[i].reserved1 = SWAP_SLOT_NONE;
            break;
        }
    }

    return 0;
}

uint32_t scheduler_current_uid(void) {
    sched_proc_t* proc = current_proc();
    return proc ? proc->uid : 0u;
}

int scheduler_login_current(const char* user_name) {
    sched_proc_t* proc = current_proc();
    int user_slot;

    if (!proc || !user_name) {
        return -1;
    }

    user_slot = find_user_slot_by_name(user_name);
    if (user_slot < 0) {
        return -1;
    }

    proc->uid = g_users[user_slot].uid;
    return 0;
}

int scheduler_current_is_user_mode(void) {
    sched_proc_t* proc = current_proc();
    return (proc && proc->user_mode) ? 1 : 0;
}

int scheduler_current_user_region(uint64_t* out_base, uint64_t* out_end) {
    sched_proc_t* proc = current_proc();

    if (!out_base || !out_end) {
        return -1;
    }
    if (!proc || !proc->user_mode || proc->user_region_base == 0u ||
        proc->user_region_end <= proc->user_region_base) {
        return -1;
    }

    *out_base = proc->user_region_base;
    *out_end = proc->user_region_end;
    return 0;
}

const char* scheduler_current_cwd(void) {
    sched_proc_t* proc = current_proc();
    return proc ? proc->cwd : "/";
}

int scheduler_setcwd_current(const char* abs_path) {
    sched_proc_t* proc = current_proc();
    if (!proc || !abs_path) {
        return -1;
    }
    str_copy(proc->cwd, abs_path, sizeof(proc->cwd));
    return 0;
}

int scheduler_getcwd_current(char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return -1;
    }
    str_copy(out, scheduler_current_cwd(), out_size);
    return 0;
}

int scheduler_proc_name_set_current(const char* name) {
    sched_proc_t* proc = current_proc();

    if (!proc || !name || !name[0]) {
        return -1;
    }
    str_copy(proc->name, name, sizeof(proc->name));
    return 0;
}

int scheduler_proc_name_get_current(char* out, size_t out_size) {
    sched_proc_t* proc = current_proc();

    if (!proc || !out || out_size == 0u) {
        return -1;
    }
    str_copy(out, proc->name, out_size);
    return 0;
}

int scheduler_current_linux_compat(void) {
    sched_proc_t* proc = current_proc();
    return (proc && proc->user_mode && proc->linux_compat) ? 1 : 0;
}

int scheduler_linux_brk(uint64_t requested, uint64_t* out_brk) {
    sched_proc_t* proc = current_proc();
    uint64_t target;
    uint64_t prev_cr3;

    if (!proc || !out_brk || !proc->user_mode || !proc->linux_compat) {
        return -1;
    }

    if (proc->linux_brk_base == 0u) {
        proc->linux_brk_base = align_up(proc->user_image_base + proc->user_image_size, PAGE_SIZE);
        proc->linux_brk_current = proc->linux_brk_base;
        proc->linux_brk_limit = proc->linux_brk_base;
    }

    if (requested == 0u) {
        *out_brk = proc->linux_brk_current;
        return 0;
    }
    if (requested < proc->linux_brk_base || requested >= proc->user_region_end) {
        *out_brk = proc->linux_brk_current;
        return 0;
    }

    target = align_up(requested, PAGE_SIZE);
    if (target < proc->linux_brk_base || target > proc->user_region_end) {
        *out_brk = proc->linux_brk_current;
        return 0;
    }

    if (target > proc->linux_brk_limit) {
        uint64_t grow = target - proc->linux_brk_limit;
        if (grow == 0u) {
            proc->linux_brk_limit = target;
        } else {
            prev_cr3 = paging_current_cr3();
            if (prev_cr3 != proc->page_table_cr3) {
                if (paging_switch_to(proc->page_table_cr3) != 0) {
                    *out_brk = proc->linux_brk_current;
                    return 0;
                }
            }
            if (paging_alloc_user_range(proc->linux_brk_limit, grow, 1u) != 0) {
                if (prev_cr3 != proc->page_table_cr3) {
                    (void)paging_switch_to(prev_cr3);
                }
                *out_brk = proc->linux_brk_current;
                return 0;
            }
            if (prev_cr3 != proc->page_table_cr3) {
                (void)paging_switch_to(prev_cr3);
            }
            proc->linux_brk_limit = target;
        }
    }

    proc->linux_brk_current = requested;
    *out_brk = proc->linux_brk_current;
    return 0;
}

int scheduler_linux_fs_base_set(uint64_t fs_base) {
    sched_proc_t* proc = current_proc();

    if (!proc || !proc->user_mode || !proc->linux_compat) {
        return -1;
    }
    proc->user_fs_base = fs_base;
    load_proc_fs_base(proc);
    return 0;
}

int scheduler_linux_fs_base_get(uint64_t* out_fs_base) {
    sched_proc_t* proc = current_proc();

    if (!proc || !out_fs_base || !proc->user_mode || !proc->linux_compat) {
        return -1;
    }
    *out_fs_base = proc->user_fs_base;
    return 0;
}
