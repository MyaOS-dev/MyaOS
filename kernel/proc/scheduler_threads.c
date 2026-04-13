#include "scheduler_internal.h"

int scheduler_thread_create_current(uint64_t entry, uint64_t arg, uint64_t stack_top, int32_t* out_tid) {
    sched_proc_t* parent = current_proc();
    sched_proc_t* thread;
    int slot;
    uint64_t start_rsp;
    uint64_t prev_cr3;

    if (!parent || !parent->user_mode || entry == 0u || parent->page_table_cr3 == 0u) {
        return -1;
    }
    if (stack_top <= parent->user_region_base + sizeof(uint64_t) || stack_top > parent->user_region_end) {
        return -1;
    }
    if (parent->user_trampoline == 0u) {
        return -1;
    }

    start_rsp = stack_top - sizeof(uint64_t);
    if (!paging_user_range_accessible(start_rsp, sizeof(uint64_t), 1u)) {
        return -1;
    }

    slot = alloc_proc_slot();
    if (slot < 0) {
        return -1;
    }
    if (init_proc_common(
            (uint32_t)slot,
            "thread",
            0,
            NULL,
            parent->cwd,
            parent->background,
            parent->pid
        ) != 0) {
        return -1;
    }

    thread = &g_procs[slot];
    if (thread->argv_block) {
        kfree(thread->argv_block);
        thread->argv_block = NULL;
        thread->argv = NULL;
        thread->argc = 0;
    }

    thread->entry = NULL;
    thread->thread_mode = 1u;
    thread->linux_compat = parent->linux_compat;
    thread->thread_group = parent->thread_group ? parent->thread_group : parent->pid;
    thread->thread_arg = arg;
    thread->user_mode = 1u;
    thread->page_table_cr3 = parent->page_table_cr3;
    thread->vm_limit_bytes = parent->vm_limit_bytes;
    thread->user_image_base = parent->user_image_base;
    thread->user_image_size = parent->user_image_size;
    thread->user_argv_base = parent->user_argv_base;
    thread->user_argv_size = parent->user_argv_size;
    thread->user_stack_base = parent->user_stack_base;
    thread->user_stack_size = parent->user_stack_size;
    thread->user_trampoline = parent->user_trampoline;
    thread->user_region_base = parent->user_region_base;
    thread->user_region_end = parent->user_region_end;
    thread->user_mmap_next = parent->user_mmap_next;
    thread->user_fs_base = parent->user_fs_base;
    thread->linux_brk_base = parent->linux_brk_base;
    thread->linux_brk_current = parent->linux_brk_current;
    thread->linux_brk_limit = parent->linux_brk_limit;
    mem_copy(thread->user_maps, parent->user_maps, sizeof(thread->user_maps));
    thread->user_entry = entry;
    thread->user_rsp = start_rsp;
    thread->user_argv_ptr = 0u;
    str_copy(thread->name, "thread", sizeof(thread->name));

    prev_cr3 = paging_current_cr3();
    if (paging_switch_to(thread->page_table_cr3) != 0) {
        release_proc_slot(thread);
        return -1;
    }
    *((uint64_t*)(uintptr_t)start_rsp) = thread->user_trampoline;
    (void)paging_switch_to(prev_cr3);

    thread->saved_rsp = make_initial_context((uint32_t)slot);
    if (thread->saved_rsp == 0u) {
        release_proc_slot(thread);
        return -1;
    }

    if (out_tid) {
        *out_tid = (int32_t)thread->pid;
    }
    return 0;
}

int scheduler_linux_clone_current(
    uint64_t frame_rsp,
    uint64_t flags,
    uint64_t child_stack,
    int32_t* parent_tid_ptr,
    int32_t* child_tid_ptr,
    uint64_t tls,
    int32_t* out_tid
) {
    sched_proc_t* parent = current_proc();
    sched_proc_t* thread;
    isr_context_t* parent_frame;
    isr_context_t* child_frame;
    uint64_t next_rip;
    uint64_t next_rsp;
    uint64_t unsupported;
    uint8_t thread_style = 0u;
    uint8_t process_style = 0u;
    uint8_t shared_vm = 0u;
    uint8_t deep_copy_process_vm = 1u;
    int slot;
    int32_t tid;

    if (!parent || !out_tid || frame_rsp == 0u || !parent->user_mode || !parent->linux_compat ||
        parent->page_table_cr3 == 0u) {
        return -1;
    }

    unsupported = flags & ~(LINUX_CLONE_SUPPORTED_FLAGS | LINUX_CLONE_SIGNAL_MASK);
    if (unsupported != 0u) {
        return -1;
    }

    thread_style = ((flags & (LINUX_CLONE_VM | LINUX_CLONE_THREAD | LINUX_CLONE_SIGHAND)) ==
                    (LINUX_CLONE_VM | LINUX_CLONE_THREAD | LINUX_CLONE_SIGHAND))
                       ? 1u
                       : 0u;
    process_style = ((flags & (LINUX_CLONE_THREAD | LINUX_CLONE_SIGHAND)) == 0u) ? 1u : 0u;
    shared_vm = ((flags & LINUX_CLONE_VM) != 0u) ? 1u : 0u;

    if (!thread_style && !process_style) {
        return -1;
    }

    parent_frame = (isr_context_t*)(uintptr_t)frame_rsp;
    if ((parent_frame->cs & 0x3u) != 0x3u) {
        return -1;
    }

    next_rip = parent_frame->rip + 2u;
    next_rsp = child_stack ? child_stack : parent_frame->rsp;
    if (next_rsp <= parent->user_region_base || next_rsp > parent->user_region_end) {
        return -1;
    }
    if (!paging_user_range_accessible(next_rsp - 1u, 1u, 0u)) {
        return -1;
    }
    if ((flags & LINUX_CLONE_PARENT_SETTID) != 0u) {
        if (!parent_tid_ptr ||
            !paging_user_range_accessible((uint64_t)(uintptr_t)parent_tid_ptr, sizeof(*parent_tid_ptr), 1u)) {
            return -1;
        }
    }
    if ((flags & LINUX_CLONE_CHILD_SETTID) != 0u || (flags & LINUX_CLONE_CHILD_CLEARTID) != 0u) {
        if (!child_tid_ptr ||
            !paging_user_range_accessible((uint64_t)(uintptr_t)child_tid_ptr, sizeof(*child_tid_ptr), 1u)) {
            return -1;
        }
    }

    slot = alloc_proc_slot();
    if (slot < 0) {
        return -1;
    }
    if (init_proc_common(
            (uint32_t)slot,
            "thread",
            0,
            NULL,
            parent->cwd,
            parent->background,
            parent->pid
        ) != 0) {
        return -1;
    }

    thread = &g_procs[slot];
    if (thread->argv_block) {
        kfree(thread->argv_block);
        thread->argv_block = NULL;
        thread->argv = NULL;
        thread->argc = 0;
    }

    thread->entry = NULL;
    thread->thread_mode = thread_style ? 1u : 0u;
    thread->linux_compat = 1u;
    thread->thread_group = thread_style ? (parent->thread_group ? parent->thread_group : parent->pid) : thread->pid;
    thread->thread_arg = 0u;
    thread->priority = parent->priority;
    thread->sched_budget = priority_budget(thread->priority);
    thread->user_mode = 1u;
    thread->page_table_cr3 = parent->page_table_cr3;
    thread->vm_limit_bytes = parent->vm_limit_bytes;
    thread->user_image_base = parent->user_image_base;
    thread->user_image_size = parent->user_image_size;
    thread->user_argv_base = parent->user_argv_base;
    thread->user_argv_size = parent->user_argv_size;
    thread->user_stack_base = parent->user_stack_base;
    thread->user_stack_size = parent->user_stack_size;
    thread->user_trampoline = parent->user_trampoline;
    thread->user_region_base = parent->user_region_base;
    thread->user_region_end = parent->user_region_end;
    thread->user_mmap_next = parent->user_mmap_next;
    thread->user_fs_base = ((flags & LINUX_CLONE_SETTLS) != 0u) ? tls : parent->user_fs_base;
    thread->linux_brk_base = parent->linux_brk_base;
    thread->linux_brk_current = parent->linux_brk_current;
    thread->linux_brk_limit = parent->linux_brk_limit;
    mem_copy(thread->user_maps, parent->user_maps, sizeof(thread->user_maps));
    thread->user_entry = next_rip;
    thread->user_rsp = next_rsp;
    thread->user_argv_ptr = parent->user_argv_ptr;
    str_copy(thread->name, parent->name, sizeof(thread->name));
    thread->wait_vfork = 0u;

    if (process_style && !shared_vm && deep_copy_process_vm) {
        if (clone_process_user_space(parent, thread) != 0) {
            release_proc_slot(thread);
            return -1;
        }
    }

    child_frame = (isr_context_t*)(uintptr_t)(thread->kernel_stack_top - sizeof(isr_context_t));
    if (!child_frame) {
        release_proc_slot(thread);
        return -1;
    }
    mem_copy(child_frame, parent_frame, sizeof(*child_frame));
    child_frame->rax = 0u;
    child_frame->rip = next_rip;
    child_frame->rsp = next_rsp;
    thread->saved_rsp = (uint64_t)(uintptr_t)child_frame;

    tid = (int32_t)thread->pid;
    if ((flags & LINUX_CLONE_PARENT_SETTID) != 0u) {
        proc_write_i32_ptr(parent, parent_tid_ptr, tid);
    }
    if ((flags & LINUX_CLONE_CHILD_SETTID) != 0u) {
        proc_write_i32_ptr(thread, child_tid_ptr, tid);
    }
    if (process_style && shared_vm) {
        /* Shared-VM process-style clone is treated as vfork-style: parent
           stays blocked until child exec/exit to avoid shared-stack races. */
        parent->state = MYAOS_PROC_BLOCKED;
        parent->wait_pid = tid;
        parent->wait_exit_ptr = NULL;
        parent->wait_notify_active = 0u;
        parent->wait_notify_clear = 0u;
        parent->wait_notify_reserved0 = 0u;
        parent->wait_notify_mask = 0u;
        parent->wait_notify_out_ptr = NULL;
        parent->wait_notify_deadline = 0u;
        parent->wait_vfork = 1u;
        g_resched_mode = RESCHED_SAVE;
    }

    *out_tid = tid;
    return 0;
}

static int can_join_thread(sched_proc_t* caller, int32_t tid) {
    sched_proc_t* target;
    uint32_t caller_group;
    uint32_t target_group;

    if (!caller) {
        return -1;
    }

    target = find_proc_by_pid(tid);
    if (!target || !target->thread_mode) {
        return -1;
    }

    caller_group = caller->thread_group ? caller->thread_group : caller->pid;
    target_group = target->thread_group ? target->thread_group : target->pid;
    if (caller_group == 0u || target_group == 0u || caller_group != target_group) {
        return -1;
    }

    return 0;
}

int scheduler_thread_join_current(int32_t tid, int32_t* exit_code) {
    sched_proc_t* caller = current_proc();

    if (!caller || tid <= 0) {
        return -1;
    }
    if (can_join_thread(caller, tid) != 0) {
        return -1;
    }

    return scheduler_wait(tid, exit_code);
}

int scheduler_thread_join_poll_current(int32_t tid, int32_t* exit_code) {
    sched_proc_t* caller = current_proc();

    if (!caller || tid <= 0) {
        return -1;
    }
    if (can_join_thread(caller, tid) != 0) {
        return -1;
    }

    return scheduler_wait_poll(tid, exit_code);
}

int scheduler_set_limits_current(const myaos_proc_limits_t* limits) {
    sched_proc_t* proc = current_proc();
    uint64_t mapped;

    if (!proc || !limits) {
        return -1;
    }

    mapped = proc_user_mapped_bytes(proc);
    if (limits->vm_limit_bytes != 0u && mapped > limits->vm_limit_bytes) {
        return -1;
    }
    if (limits->cpu_limit_ticks != 0u && proc->cpu_ticks_used >= limits->cpu_limit_ticks) {
        return -1;
    }

    proc->cpu_limit_ticks = limits->cpu_limit_ticks;
    proc->vm_limit_bytes = limits->vm_limit_bytes;

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (!g_procs[i].used || &g_procs[i] == proc) {
            continue;
        }
        if (!g_procs[i].user_mode || g_procs[i].page_table_cr3 != proc->page_table_cr3) {
            continue;
        }
        g_procs[i].vm_limit_bytes = limits->vm_limit_bytes;
    }

    return 0;
}

int scheduler_get_limits_current(myaos_proc_limits_t* out) {
    sched_proc_t* proc = current_proc();

    if (!proc || !out) {
        return -1;
    }

    out->cpu_limit_ticks = proc->cpu_limit_ticks;
    out->reserved0 = 0u;
    out->vm_limit_bytes = proc->vm_limit_bytes;
    return 0;
}

