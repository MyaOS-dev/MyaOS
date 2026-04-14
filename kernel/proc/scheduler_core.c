#include "scheduler_internal.h"

void scheduler_init(uint32_t slice_ticks) {
    if (slice_ticks == 0) {
        slice_ticks = 1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        mem_zero(&g_procs[i], sizeof(g_procs[i]));
        g_procs[i].wait_pid = -1;
    }
    for (uint32_t i = 0; i < SCHED_MAX_PIPES; i++) {
        mem_zero(&g_pipes[i], sizeof(g_pipes[i]));
    }
    for (uint32_t i = 0; i < SCHED_MAX_USERS; i++) {
        mem_zero(&g_users[i], sizeof(g_users[i]));
    }
    for (uint32_t i = 0; i < SCHED_MAX_SHM_SEGMENTS; i++) {
        mem_zero(&g_shm[i], sizeof(g_shm[i]));
    }

    g_users[0].used = 1u;
    g_users[0].uid = 0u;
    str_copy(g_users[0].name, "root", sizeof(g_users[0].name));
    g_users[1].used = 1u;
    g_users[1].uid = 1000u;
    str_copy(g_users[1].name, "user", sizeof(g_users[1].name));
    g_users[2].used = 1u;
    g_users[2].uid = 1001u;
    str_copy(g_users[2].name, "guest", sizeof(g_users[2].name));

    g_proc_count = 0;
    g_current_slot = SCHED_INVALID_SLOT;
    g_next_pid = 1;
    g_slice_ticks = slice_ticks;
    g_last_switch_tick = 0;
    g_started = 0;
    g_kernel_cs = gdt_kernel_code_selector();
    g_kernel_ss = gdt_kernel_data_selector();
    g_user_cs = gdt_user_code_selector();
    g_user_ss = gdt_user_data_selector();
    g_resched_mode = RESCHED_NONE;
}

int scheduler_spawn_kernel(
    const char* name,
    process_entry_t entry,
    int argc,
    const char* const* argv,
    uint8_t background,
    int32_t* out_pid
) {
    int slot = alloc_proc_slot();
    uint32_t ppid = current_proc() ? current_proc()->pid : 0;

    if (slot < 0 || !entry) {
        return -1;
    }
    if (init_proc_common((uint32_t)slot, name, argc, argv, scheduler_current_cwd(), background, ppid) != 0) {
        return -1;
    }

    g_procs[slot].entry = entry;
    g_procs[slot].user_mode = 0;
    g_procs[slot].saved_rsp = make_initial_context((uint32_t)slot);
    if (g_procs[slot].saved_rsp == 0) {
        release_proc_slot(&g_procs[slot]);
        return -1;
    }

    if (out_pid) {
        *out_pid = (int32_t)g_procs[slot].pid;
    }
    return 0;
}

static void apply_spawn_opts(sched_proc_t* proc, uint8_t background, const myaos_spawn_opts_t* opts) {
    if (!proc) {
        return;
    }

    proc->background = background;
    proc->priority = background ? MYAOS_PROC_PRIO_LOW : MYAOS_PROC_PRIO_NORMAL;
    proc->sched_budget = priority_budget(proc->priority);
    proc->cpu_limit_ticks = 0u;
    proc->vm_limit_bytes = 0u;
    proc->cpu_ticks_used = 0u;
    proc->linux_compat = 0u;
    proc->stdout_redirect = 0;
    proc->stdout_append = 0;
    proc->stdout_truncated = 0;
    proc->stdout_path[0] = '\0';

    if (!opts) {
        return;
    }

    if ((opts->flags & MYAOS_SPAWN_BACKGROUND) != 0u) {
        proc->background = 1;
    }
    if ((opts->flags & MYAOS_SPAWN_LINUX) != 0u) {
        proc->linux_compat = 1u;
    }
    if (opts->priority != 0u) {
        proc->priority = sanitize_priority(opts->priority);
        proc->sched_budget = priority_budget(proc->priority);
    }
    if (opts->cpu_limit_ticks != 0u) {
        proc->cpu_limit_ticks = opts->cpu_limit_ticks;
    }
    if ((opts->flags & MYAOS_SPAWN_STDOUT_REDIRECT) != 0u && opts->stdout_path[0]) {
        proc->stdout_redirect = 1;
        proc->stdout_append = opts->stdout_append ? 1 : 0;
        proc->stdout_truncated = 0;
        str_copy(proc->stdout_path, opts->stdout_path, sizeof(proc->stdout_path));
    }
}

int scheduler_spawn_program_ex(
    const char* cwd,
    const char* path,
    int argc,
    const char* const* argv,
    const myaos_spawn_opts_t* opts,
    int32_t* out_pid
) {
    int slot = alloc_proc_slot();
    elf_image_t image;
    elf_image_t interp_image;
    uint8_t use_interp = 0u;
    char name[SCHED_PROC_NAME_MAX];
    char fallback_path[MYAOS_PATH_MAX];
    const char* load_path = path;
    uint32_t ppid = current_proc() ? current_proc()->pid : 0;
    const char* proc_cwd = cwd && cwd[0] ? cwd : scheduler_current_cwd();
    uint8_t background = (opts && (opts->flags & MYAOS_SPAWN_BACKGROUND) != 0u) ? 1u : 0u;

    mem_zero(&interp_image, sizeof(interp_image));

    if (slot < 0) {
        return -1;
    }
    if (vfs_can_exec(proc_cwd, load_path) != 0 || elf_load_from_vfs(proc_cwd, load_path, &image) != 0) {
        if (!str_starts_with(path, "/bin/")) {
            return -1;
        }
        str_copy(fallback_path, "/boot/bin/", sizeof(fallback_path));
        {
            size_t base = str_len(fallback_path);
            const char* suffix = path + 5;
            for (size_t i = 0; suffix[i] && base + 1u < sizeof(fallback_path); i++) {
                fallback_path[base++] = suffix[i];
            }
            fallback_path[base] = '\0';
        }
        load_path = fallback_path;
        if (vfs_can_exec(proc_cwd, load_path) != 0 || elf_load_from_vfs(proc_cwd, load_path, &image) != 0) {
            return -1;
        }
    }

    basename_copy(name, load_path, sizeof(name));
    if (init_proc_common((uint32_t)slot, name, argc, argv, proc_cwd, background, ppid) != 0) {
        elf_unload(&image);
        return -1;
    }
    apply_spawn_opts(&g_procs[slot], background, opts);
    if (g_procs[slot].linux_compat && image.interp[0]) {
        if (load_linux_interpreter_image(image.interp, &interp_image) != 0) {
            release_proc_slot(&g_procs[slot]);
            elf_unload(&image);
            return -1;
        }
        use_interp = 1u;
    }
    if (paging_space_create(&g_procs[slot].page_table_cr3) != 0) {
        release_proc_slot(&g_procs[slot]);
        elf_unload(&image);
        if (use_interp) {
            elf_unload(&interp_image);
        }
        return -1;
    }

    if (setup_user_process_image(&g_procs[slot], (uint32_t)slot, &image, use_interp ? &interp_image : NULL) != 0) {
        release_proc_slot(&g_procs[slot]);
        elf_unload(&image);
        if (use_interp) {
            elf_unload(&interp_image);
        }
        return -1;
    }

    g_procs[slot].saved_rsp = make_initial_context((uint32_t)slot);
    if (g_procs[slot].saved_rsp == 0) {
        release_proc_slot(&g_procs[slot]);
        elf_unload(&image);
        if (use_interp) {
            elf_unload(&interp_image);
        }
        return -1;
    }

    elf_unload(&image);
    if (use_interp) {
        elf_unload(&interp_image);
    }
    if (out_pid) {
        *out_pid = (int32_t)g_procs[slot].pid;
    }
    return 0;
}

int scheduler_spawn_program(
    const char* cwd,
    const char* path,
    int argc,
    const char* const* argv,
    uint8_t background,
    int32_t* out_pid
) {
    myaos_spawn_opts_t opts;

    mem_zero(&opts, sizeof(opts));
    if (background) {
        opts.flags |= MYAOS_SPAWN_BACKGROUND;
    }

    return scheduler_spawn_program_ex(cwd, path, argc, argv, &opts, out_pid);
}

int scheduler_exec_current(const char* path, int argc, const char* const* argv) {
    sched_proc_t* proc = current_proc();
    elf_image_t image;
    elf_image_t interp_image;
    uint8_t use_interp = 0u;
    char** argv_copy = NULL;
    void* argv_block = NULL;
    char exec_path[MYAOS_PATH_MAX];
    uint32_t exec_len = 0u;
    uint64_t new_cr3 = 0;
    uint8_t needs_new_space;

    mem_zero(&interp_image, sizeof(interp_image));

    if (!proc) {
        return -1;
    }
    if (proc->thread_mode) {
        return -1;
    }
    if (!path) {
        return -1;
    }
    while (exec_len + 1u < (uint32_t)sizeof(exec_path) && path[exec_len] != '\0') {
        exec_path[exec_len] = path[exec_len];
        exec_len++;
    }
    if (path[exec_len] != '\0') {
        return -1;
    }
    exec_path[exec_len] = '\0';

    if (vfs_can_exec(proc->cwd, exec_path) != 0) {
        return -1;
    }
    if (elf_load_from_vfs(proc->cwd, exec_path, &image) != 0) {
        return -1;
    }
    if (proc->linux_compat && image.interp[0]) {
        if (load_linux_interpreter_image(image.interp, &interp_image) != 0) {
            elf_unload(&image);
            return -1;
        }
        use_interp = 1u;
    }
    if (duplicate_argv(argc, argv, &argv_copy, &argv_block) != 0) {
        if (use_interp) {
            elf_unload(&interp_image);
        }
        elf_unload(&image);
        return -1;
    }

    needs_new_space = (uint8_t)(
        proc->page_table_cr3 == paging_kernel_cr3() ||
        (proc->page_table_cr3 != 0u && count_procs_with_cr3(proc->page_table_cr3) > 1u)
    );
    if (needs_new_space && paging_space_create(&new_cr3) != 0) {
        kfree(argv_block);
        if (use_interp) {
            elf_unload(&interp_image);
        }
        elf_unload(&image);
        return -1;
    }

    free_proc_resources(proc);
    if (needs_new_space) {
        proc->page_table_cr3 = new_cr3;
    }
    proc->argc = argc;
    proc->argv = argv_copy;
    proc->argv_block = argv_block;
    proc->entry = NULL;
    proc->linux_compat = proc->linux_compat ? 1u : 0u;

    if (setup_user_process_image(proc, g_current_slot, &image, use_interp ? &interp_image : NULL) != 0) {
        if (use_interp) {
            elf_unload(&interp_image);
        }
        elf_unload(&image);
        scheduler_exit_current(-1);
        return -1;
    }

    basename_copy(proc->name, exec_path, sizeof(proc->name));
    proc->state = MYAOS_PROC_READY;
    proc->saved_rsp = make_initial_context(g_current_slot);
    if (proc->saved_rsp == 0) {
        if (use_interp) {
            elf_unload(&interp_image);
        }
        elf_unload(&image);
        scheduler_exit_current(-1);
        return -1;
    }

    if (use_interp) {
        elf_unload(&interp_image);
    }
    elf_unload(&image);
    wake_vfork_parent(proc->ppid, proc->pid);
    g_resched_mode = RESCHED_DROP;
    return 0;
}

int scheduler_wait(int32_t pid, int32_t* exit_code) {
    sched_proc_t* proc = current_proc();
    int slot;

    if (!proc) {
        return -1;
    }

    slot = find_child_slot(proc->pid, pid, 1);
    if (slot >= 0) {
        if (exit_code) {
            *exit_code = g_procs[slot].exit_code;
        }
        release_proc_slot(&g_procs[slot]);
        return 0;
    }

    slot = find_child_slot(proc->pid, pid, 0);
    if (slot < 0) {
        return -1;
    }

    proc->state = MYAOS_PROC_BLOCKED;
    proc->wait_pid = pid;
    proc->wait_exit_ptr = exit_code;
    proc->wait_notify_active = 0u;
    proc->wait_notify_clear = 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = 0u;
    proc->wait_notify_out_ptr = NULL;
    proc->wait_notify_deadline = 0u;
    proc->wait_vfork = 0u;
    g_resched_mode = RESCHED_SAVE;
    return 0;
}

int scheduler_wait_poll(int32_t pid, int32_t* exit_code) {
    sched_proc_t* proc = current_proc();
    int slot;

    if (!proc) {
        return -1;
    }

    slot = find_child_slot(proc->pid, pid, 1);
    if (slot >= 0) {
        if (exit_code) {
            *exit_code = g_procs[slot].exit_code;
        }
        release_proc_slot(&g_procs[slot]);
        return 1;
    }

    slot = find_child_slot(proc->pid, pid, 0);
    if (slot < 0) {
        return -1;
    }

    return 0;
}

int scheduler_kill_pid(int32_t pid, int32_t exit_code) {
    sched_proc_t* caller = current_proc();
    int slot = find_proc_slot_by_pid(pid);

    if (slot < 0) {
        return -1;
    }

    if (caller) {
        if ((int32_t)g_procs[slot].pid != (int32_t)caller->pid && g_procs[slot].ppid != caller->pid) {
            return -1;
        }
    }

    if (g_procs[slot].state == MYAOS_PROC_ZOMBIE || g_procs[slot].state == MYAOS_PROC_NONE) {
        return -1;
    }

    if ((uint32_t)slot == g_current_slot && caller == &g_procs[slot]) {
        scheduler_exit_current(exit_code);
        return 0;
    }

    g_procs[slot].exit_code = exit_code;
    g_procs[slot].state = MYAOS_PROC_ZOMBIE;
    g_procs[slot].wake_tick = 0;
    g_procs[slot].wait_pid = -1;
    g_procs[slot].wait_exit_ptr = NULL;
    g_procs[slot].wait_notify_active = 0u;
    g_procs[slot].wait_notify_clear = 0u;
    g_procs[slot].wait_notify_reserved0 = 0u;
    g_procs[slot].wait_notify_mask = 0u;
    g_procs[slot].wait_notify_out_ptr = NULL;
    g_procs[slot].wait_notify_deadline = 0u;
    g_procs[slot].wait_vfork = 0u;
    wake_vfork_parent(g_procs[slot].ppid, g_procs[slot].pid);
    maybe_wake_waiters((uint32_t)slot);
    return 0;
}

void scheduler_exit_current(int32_t exit_code) {
    sched_proc_t* proc = current_proc();

    if (!proc) {
        return;
    }

    proc->exit_code = exit_code;
    proc->state = MYAOS_PROC_ZOMBIE;
    proc->wait_pid = -1;
    proc->wait_exit_ptr = NULL;
    proc->wait_notify_active = 0u;
    proc->wait_notify_clear = 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = 0u;
    proc->wait_notify_out_ptr = NULL;
    proc->wait_notify_deadline = 0u;
    proc->wait_vfork = 0u;
    wake_vfork_parent(proc->ppid, proc->pid);
    maybe_wake_waiters(g_current_slot);
    g_resched_mode = RESCHED_DROP;
}

void scheduler_kill_current(int32_t exit_code) {
    scheduler_exit_current(exit_code);
}

void scheduler_yield_current(void) {
    sched_proc_t* proc = current_proc();
    if (!proc) {
        return;
    }
    if (proc->state == MYAOS_PROC_RUNNING) {
        proc->state = MYAOS_PROC_READY;
    }
    g_resched_mode = RESCHED_SAVE;
}

void scheduler_sleep_current(uint64_t ticks) {
    sched_proc_t* proc = current_proc();

    if (!proc) {
        return;
    }
    if (ticks == 0) {
        scheduler_yield_current();
        return;
    }

    proc->wake_tick = timer_ticks() + ticks;
    proc->state = MYAOS_PROC_SLEEPING;
    g_resched_mode = RESCHED_SAVE;
}

uint64_t scheduler_on_timer_interrupt(uint64_t current_rsp, uint64_t tick_now) {
    return schedule_switch(current_rsp, tick_now, 1);
}

uint64_t scheduler_on_syscall_complete(uint64_t current_rsp, uint64_t tick_now) {
    if (g_resched_mode == RESCHED_NONE) {
        return current_rsp;
    }
    return schedule_switch(current_rsp, tick_now, 0);
}

int scheduler_reschedule_pending(void) {
    return g_resched_mode != RESCHED_NONE;
}

void scheduler_task_bootstrap(void) {
    for (;;) {
        sched_proc_t* proc = current_proc();
        int exit_code = 0;

        if (!proc || !proc->entry) {
            __asm__ __volatile__("hlt");
            continue;
        }

        exit_code = proc->entry(proc->argc, proc->argv);
        scheduler_exit_current(exit_code);
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }
}

void scheduler_run(void) {
    uint32_t first = first_ready_slot();

    if (first == SCHED_INVALID_SLOT) {
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    __asm__ __volatile__("cli");

    g_current_slot = first;
    g_started = 1;
    g_last_switch_tick = timer_ticks();
    g_procs[first].state = MYAOS_PROC_RUNNING;
    if (g_procs[first].sched_budget > 0u) {
        g_procs[first].sched_budget--;
    }
    g_procs[first].run_count++;
    g_procs[first].last_run_tick = g_last_switch_tick;
    gdt_set_kernel_stack(g_procs[first].kernel_stack_top);
    (void)paging_switch_to(g_procs[first].page_table_cr3);
    load_proc_fs_base(&g_procs[first]);

    scheduler_restore_and_enter(g_procs[first].saved_rsp);
}

uint32_t scheduler_process_count(void) {
    return g_proc_count;
}

int32_t scheduler_current_pid(void) {
    sched_proc_t* proc = current_proc();
    return proc ? (int32_t)proc->pid : -1;
}

int scheduler_list_processes(myaos_proc_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t count = 0;

    if (!out_count || (max_entries != 0 && !out)) {
        return -1;
    }

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        if (!g_procs[i].used) {
            continue;
        }
        if (count >= max_entries) {
            break;
        }

        out[count].pid = g_procs[i].pid;
        out[count].ppid = g_procs[i].ppid;
        str_copy(out[count].name, g_procs[i].name, sizeof(out[count].name));
        out[count].state = g_procs[i].state;
        out[count].background = g_procs[i].background;
        out[count].priority = sanitize_priority(g_procs[i].priority);
        out[count].reserved0 = 0;
        out[count].exit_code = g_procs[i].exit_code;
        out[count].cpu_limit_ticks = g_procs[i].cpu_limit_ticks;
        out[count].reserved1 = 0;
        out[count].cpu_ticks_used = g_procs[i].cpu_ticks_used;
        out[count].wake_tick = g_procs[i].wake_tick;
        out[count].run_count = g_procs[i].run_count;
        out[count].last_run_tick = g_procs[i].last_run_tick;
        out[count].vm_limit_bytes = g_procs[i].vm_limit_bytes;
        out[count].vm_used_bytes = proc_user_mapped_bytes(&g_procs[i]);
        count++;
    }

    *out_count = count;
    return 0;
}

