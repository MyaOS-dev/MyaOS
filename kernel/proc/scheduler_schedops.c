#include "scheduler_internal.h"

void free_proc_resources(sched_proc_t* proc) {
   uint8_t can_free_user = 1u;

   if (!proc) {
       return;
   }

   if (proc->argv_block) {
       kfree(proc->argv_block);
       proc->argv_block = NULL;
       proc->argv = NULL;
       proc->argc = 0;
   }

   if (proc->user_mode && proc->page_table_cr3 != 0u && proc->page_table_cr3 != paging_kernel_cr3()) {
       if (count_procs_with_cr3(proc->page_table_cr3) > 1u) {
           can_free_user = 0u;
       }
   }

   if (can_free_user) {
       free_user_space(proc);
   } else {
       clear_user_meta(proc);
   }
}

void release_proc_slot(sched_proc_t* proc) {
   uint64_t proc_cr3;
   uint8_t destroy_space = 0u;

   if (!proc || !proc->used) {
       return;
   }

   proc_cr3 = proc->page_table_cr3;
   close_all_proc_pipe_handles(proc);
   close_all_proc_file_handles(proc);
   free_proc_resources(proc);
   if (proc_cr3 != 0u && proc_cr3 != paging_kernel_cr3() && count_procs_with_cr3(proc_cr3) <= 1u) {
       destroy_space = 1u;
   }
   if (destroy_space) {
       paging_space_destroy(proc_cr3);
   }
   if (proc->kernel_stack_raw) {
       kfree(proc->kernel_stack_raw);
       proc->kernel_stack_raw = NULL;
   }

   net_on_process_exit((int32_t)proc->pid);

   proc->used = 0;
   proc->pid = 0;
   proc->ppid = 0;
   proc->uid = 0;
   proc->thread_group = 0;
   proc->thread_arg = 0;
   proc->thread_mode = 0;
   proc->linux_compat = 0;
   proc->name[0] = '\0';
   proc->state = MYAOS_PROC_NONE;
   proc->background = 0;
   proc->priority = MYAOS_PROC_PRIO_NORMAL;
   proc->sched_budget = 0;
   proc->cpu_limit_ticks = 0;
   proc->vm_limit_bytes = 0u;
   proc->cpu_ticks_used = 0;
   proc->saved_rsp = 0;
   proc->wake_tick = 0;
   proc->run_count = 0;
   proc->last_run_tick = 0;
   proc->exit_code = 0;
   proc->wait_pid = -1;
   proc->wait_exit_ptr = NULL;
   proc->wait_notify_active = 0u;
   proc->wait_notify_clear = 0u;
   proc->wait_notify_reserved0 = 0u;
   proc->wait_notify_mask = 0u;
   proc->wait_notify_out_ptr = NULL;
   proc->wait_notify_deadline = 0u;
   proc->wait_vfork = 0u;
   proc->cwd[0] = '\0';
   proc->entry = NULL;
   proc->kernel_stack_top = 0;
   proc->page_table_cr3 = 0;
   proc->user_region_base = 0;
   proc->user_region_end = 0;
   clear_user_meta(proc);
   mem_zero(proc->pipe_handles, sizeof(proc->pipe_handles));
   mem_zero(proc->file_handles, sizeof(proc->file_handles));
   proc->stdout_redirect = 0;
   proc->stdout_append = 0;
   proc->stdout_truncated = 0;
   proc->stdout_path[0] = '\0';
   proc->ipc_full = 0;
   proc->notify_bits = 0;
   proc->ipc_from = 0;
   proc->ipc_len = 0;
   proc->ipc_data[0] = '\0';
   if (g_proc_count > 0) {
       g_proc_count--;
   }
}

int alloc_proc_slot(void) {
   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       if (!g_procs[i].used) {
           return (int)i;
       }
   }
   return -1;
}

int init_proc_common(
   uint32_t slot,
   const char* name,
   int argc,
   const char* const* argv,
   const char* cwd,
   uint8_t background,
   uint32_t ppid
) {
   sched_proc_t* proc = &g_procs[slot];
   sched_proc_t* parent = current_proc();

   mem_zero(proc, sizeof(*proc));
   proc->used = 1;
   proc->pid = g_next_pid++;
   proc->ppid = ppid;
   proc->uid = parent ? parent->uid : 0u;
   proc->thread_mode = 0u;
   proc->linux_compat = 0u;
   proc->thread_group = proc->pid;
   proc->thread_arg = 0u;
   str_copy(proc->name, name ? name : "proc", sizeof(proc->name));
   proc->state = MYAOS_PROC_READY;
   proc->background = background;
   proc->priority = background ? MYAOS_PROC_PRIO_LOW : MYAOS_PROC_PRIO_NORMAL;
   proc->sched_budget = priority_budget(proc->priority);
   proc->cpu_limit_ticks = 0;
   proc->vm_limit_bytes = 0u;
   proc->cpu_ticks_used = 0;
   proc->exit_code = 0;
   proc->wait_pid = -1;
   proc->wait_exit_ptr = NULL;
   proc->wait_notify_active = 0u;
   proc->wait_notify_clear = 0u;
   proc->wait_notify_reserved0 = 0u;
   proc->wait_notify_mask = 0u;
   proc->wait_notify_out_ptr = NULL;
   proc->wait_notify_deadline = 0u;
   proc->wait_vfork = 0u;
   proc->page_table_cr3 = paging_kernel_cr3();
   proc->user_region_base = 0u;
   proc->user_region_end = 0u;
   str_copy(proc->cwd, cwd && cwd[0] ? cwd : "/", sizeof(proc->cwd));
   proc->stdout_redirect = 0;
   proc->stdout_append = 0;
   proc->stdout_truncated = 0;
   proc->stdout_path[0] = '\0';
   proc->ipc_full = 0;
   proc->notify_bits = 0;
   proc->ipc_from = 0;
   proc->ipc_len = 0;

   if (parent) {
       for (uint32_t i = 0; i < SCHED_MAX_PIPE_HANDLES; i++) {
           uint32_t pipe_slot;
           uint8_t mode;

           if (!parent->pipe_handles[i].used) {
               continue;
           }
           pipe_slot = parent->pipe_handles[i].pipe_slot;
           mode = parent->pipe_handles[i].mode;
           if (!pipe_slot_valid(pipe_slot)) {
               continue;
           }

           proc->pipe_handles[i] = parent->pipe_handles[i];
           if ((mode & PIPE_MODE_READ) != 0u) {
               g_pipes[pipe_slot].readers++;
           }
           if ((mode & PIPE_MODE_WRITE) != 0u) {
               g_pipes[pipe_slot].writers++;
           }
       }

       for (uint32_t i = 0; i < SCHED_MAX_FILE_HANDLES; i++) {
           if (!parent->file_handles[i].used) {
               continue;
           }
           if (clone_proc_file_handle(&proc->file_handles[i], &parent->file_handles[i]) != 0) {
               close_all_proc_pipe_handles(proc);
               close_all_proc_file_handles(proc);
               proc->used = 0;
               return -1;
           }
       }
   }

   proc->kernel_stack_raw = kmalloc(SCHED_STACK_SIZE + 16u);
   if (!proc->kernel_stack_raw) {
       close_all_proc_pipe_handles(proc);
       close_all_proc_file_handles(proc);
       proc->used = 0;
       return -1;
   }

   proc->kernel_stack_top = ((uint64_t)(uintptr_t)proc->kernel_stack_raw + (uint64_t)SCHED_STACK_SIZE) & ~0xFULL;

   if (duplicate_argv(argc, argv, &proc->argv, &proc->argv_block) != 0) {
       kfree(proc->kernel_stack_raw);
       proc->kernel_stack_raw = NULL;
       proc->kernel_stack_top = 0;
       close_all_proc_pipe_handles(proc);
       close_all_proc_file_handles(proc);
       proc->used = 0;
       return -1;
   }
   proc->argc = argc;

   g_proc_count++;
   return 0;
}

void wake_sleepers(uint64_t tick_now) {
   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       sched_proc_t* proc = &g_procs[i];

       if (!proc->used) {
           continue;
       }
       if (proc->state == MYAOS_PROC_SLEEPING && proc->wake_tick <= tick_now) {
           proc->state = MYAOS_PROC_READY;
           proc->wake_tick = 0;
           continue;
       }
       if (proc->state == MYAOS_PROC_BLOCKED &&
           proc->wait_notify_active &&
           proc->wait_notify_deadline != 0u &&
           proc->wait_notify_deadline <= tick_now) {
           if (proc->wait_notify_out_ptr) {
               proc_write_u32_ptr(proc, proc->wait_notify_out_ptr, 0u);
           }
           proc->wait_notify_active = 0u;
           proc->wait_notify_clear = 0u;
           proc->wait_notify_reserved0 = 0u;
           proc->wait_notify_mask = 0u;
           proc->wait_notify_out_ptr = NULL;
           proc->wait_notify_deadline = 0u;
           proc->wait_pid = -1;
           proc->wait_vfork = 0u;
           proc->state = MYAOS_PROC_READY;
       }
   }
}

void maybe_wake_notify_waiter(sched_proc_t* proc) {
   uint32_t mask;
   uint32_t bits;

   if (!proc || !proc->used || proc->state != MYAOS_PROC_BLOCKED || !proc->wait_notify_active) {
       return;
   }

   mask = proc->wait_notify_mask ? proc->wait_notify_mask : 0xFFFFFFFFu;
   bits = proc->notify_bits & mask;
   if (bits == 0u) {
       return;
   }

   if (proc->wait_notify_out_ptr) {
       proc_write_u32_ptr(proc, proc->wait_notify_out_ptr, bits);
   }
   if (proc->wait_notify_clear) {
       proc->notify_bits &= ~bits;
   }

   proc->wait_notify_active = 0u;
   proc->wait_notify_clear = 0u;
   proc->wait_notify_reserved0 = 0u;
   proc->wait_notify_mask = 0u;
   proc->wait_notify_out_ptr = NULL;
   proc->wait_notify_deadline = 0u;
   proc->wait_pid = -1;
   proc->wait_vfork = 0u;
   proc->state = MYAOS_PROC_READY;
}

void maybe_wake_waiters(uint32_t child_slot) {
   sched_proc_t* child = &g_procs[child_slot];

   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       sched_proc_t* proc = &g_procs[i];
       if (!proc->used || proc->state != MYAOS_PROC_BLOCKED) {
           continue;
       }
       if (proc->wait_notify_active) {
           continue;
       }
       if (proc->wait_pid != -1 && proc->wait_pid != (int32_t)child->pid) {
           continue;
       }
       if (proc->wait_exit_ptr) {
           proc_write_i32_ptr(proc, proc->wait_exit_ptr, child->exit_code);
       }
       proc->wait_exit_ptr = NULL;
       proc->wait_pid = -1;
       proc->wait_vfork = 0u;
       proc->state = MYAOS_PROC_READY;
   }
}

void wake_vfork_parent(uint32_t parent_pid, uint32_t child_pid) {
   if (parent_pid == 0u || child_pid == 0u) {
       return;
   }

   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       sched_proc_t* parent = &g_procs[i];
       if (!parent->used || parent->pid != parent_pid) {
           continue;
       }
       if (!parent->wait_vfork || parent->wait_pid != (int32_t)child_pid) {
           break;
       }
       parent->wait_vfork = 0u;
       parent->wait_pid = -1;
       parent->wait_exit_ptr = NULL;
       parent->wait_notify_active = 0u;
       parent->wait_notify_clear = 0u;
       parent->wait_notify_reserved0 = 0u;
       parent->wait_notify_mask = 0u;
       parent->wait_notify_out_ptr = NULL;
       parent->wait_notify_deadline = 0u;
       if (parent->state == MYAOS_PROC_BLOCKED) {
           parent->state = MYAOS_PROC_READY;
       }
       break;
   }
}

int find_child_slot(uint32_t parent_pid, int32_t pid, uint8_t require_zombie) {
   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       if (!g_procs[i].used || g_procs[i].ppid != parent_pid) {
           continue;
       }
       if (pid != -1 && (int32_t)g_procs[i].pid != pid) {
           continue;
       }
       if (require_zombie && g_procs[i].state != MYAOS_PROC_ZOMBIE) {
           continue;
       }
       return (int)i;
   }
   return -1;
}

uint64_t schedule_switch(uint64_t current_rsp, uint64_t tick_now, uint8_t timer_preempt) {
   sched_proc_t* current = current_proc();
   uint32_t next;

   if (timer_preempt && current && current->used && current->state == MYAOS_PROC_RUNNING && current->user_mode) {
       current->cpu_ticks_used++;
       if (current->cpu_limit_ticks != 0u && current->cpu_ticks_used >= current->cpu_limit_ticks) {
           current->exit_code = -9;
           current->state = MYAOS_PROC_ZOMBIE;
           current->wake_tick = 0;
           current->wait_pid = -1;
           current->wait_exit_ptr = NULL;
           current->wait_notify_active = 0u;
           current->wait_notify_clear = 0u;
           current->wait_notify_reserved0 = 0u;
           current->wait_notify_mask = 0u;
           current->wait_notify_out_ptr = NULL;
           current->wait_notify_deadline = 0u;
           current->wait_vfork = 0u;
           wake_vfork_parent(current->ppid, current->pid);
           maybe_wake_waiters(g_current_slot);
           g_resched_mode = RESCHED_DROP;
       }
   }

   wake_sleepers(tick_now);

   if (!g_started || g_proc_count == 0) {
       g_resched_mode = RESCHED_NONE;
       return current_rsp;
   }

   if (current && timer_preempt && g_resched_mode == RESCHED_NONE) {
       if (tick_now - g_last_switch_tick < g_slice_ticks) {
           return current_rsp;
       }
       current->saved_rsp = current_rsp;
       if (current->state == MYAOS_PROC_RUNNING) {
           current->state = MYAOS_PROC_READY;
       }
   } else if (current && g_resched_mode == RESCHED_SAVE) {
       current->saved_rsp = current_rsp;
       if (current->state == MYAOS_PROC_RUNNING) {
           current->state = MYAOS_PROC_READY;
       }
   }

   next = (g_current_slot == SCHED_INVALID_SLOT) ? first_ready_slot() : find_next_ready(g_current_slot);
   if (next == SCHED_INVALID_SLOT) {
       if (current && current->used && (current->state == MYAOS_PROC_READY || current->state == MYAOS_PROC_RUNNING)) {
           current->state = MYAOS_PROC_RUNNING;
           g_last_switch_tick = tick_now;
           g_resched_mode = RESCHED_NONE;
           gdt_set_kernel_stack(current->kernel_stack_top);
           (void)paging_switch_to(current->page_table_cr3);
           load_proc_fs_base(current);
           return current->saved_rsp ? current->saved_rsp : current_rsp;
       }
       g_resched_mode = RESCHED_NONE;
       return current_rsp;
   }

   g_current_slot = next;
   g_procs[next].state = MYAOS_PROC_RUNNING;
   if (g_procs[next].sched_budget > 0u) {
       g_procs[next].sched_budget--;
   }
   g_procs[next].run_count++;
   g_procs[next].last_run_tick = tick_now;
   g_last_switch_tick = tick_now;
   g_resched_mode = RESCHED_NONE;
   gdt_set_kernel_stack(g_procs[next].kernel_stack_top);
   (void)paging_switch_to(g_procs[next].page_table_cr3);
   load_proc_fs_base(&g_procs[next]);
   return g_procs[next].saved_rsp;
}

__attribute__((noreturn)) void scheduler_restore_and_enter(uint64_t rsp) {
   __asm__ __volatile__(
       "mov %0, %%rsp\n"
       "pop %%rax\n"
       "pop %%rbx\n"
       "pop %%rcx\n"
       "pop %%rdx\n"
       "pop %%rsi\n"
       "pop %%rdi\n"
       "pop %%rbp\n"
       "pop %%r8\n"
       "pop %%r9\n"
       "pop %%r10\n"
       "pop %%r11\n"
       "pop %%r12\n"
       "pop %%r13\n"
       "pop %%r14\n"
       "pop %%r15\n"
       "add $16, %%rsp\n"
       "iretq\n"
       :
       : "r"(rsp)
       : "memory"
   );

   __builtin_unreachable();
}

