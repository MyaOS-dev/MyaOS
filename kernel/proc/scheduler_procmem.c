#include "scheduler_internal.h"

uint64_t make_initial_context(uint32_t slot) {
   uint64_t stack_top;
   isr_context_t* frame;
   sched_proc_t* proc;

   if (slot >= SCHED_MAX_PROCS || !g_procs[slot].kernel_stack_raw) {
       return 0;
   }

   proc = &g_procs[slot];
   stack_top = proc->kernel_stack_top;
   frame = (isr_context_t*)(uintptr_t)(stack_top - sizeof(isr_context_t));
   mem_zero(frame, sizeof(*frame));

   frame->vector = 32u;
   frame->error_code = 0u;
   frame->rflags = (read_rflags() | 0x200ULL) & ~(0x3000ULL);

   if (proc->user_mode) {
       frame->rip = proc->user_entry;
       frame->cs = g_user_cs;
       frame->rsp = proc->user_rsp;
       frame->ss = g_user_ss;
       if (proc->thread_mode) {
           frame->rdi = proc->thread_arg;
           frame->rsi = 0u;
       } else if (proc->linux_compat) {
           frame->rdi = 0u;
           frame->rsi = 0u;
       } else {
           frame->rdi = (uint64_t)(uint32_t)proc->argc;
           frame->rsi = proc->user_argv_ptr;
       }
   } else {
       frame->rip = (uint64_t)(uintptr_t)scheduler_task_bootstrap;
       frame->cs = g_kernel_cs;
       /* Match SysV call-entry stack shape (RSP % 16 == 8) for kernel tasks. */
       frame->rsp = stack_top - sizeof(uint64_t);
       frame->ss = g_kernel_ss;
   }

   return (uint64_t)(uintptr_t)frame;
}

int duplicate_argv(int argc, const char* const* argv, char*** out_argv, void** out_block) {
   size_t bytes = (size_t)(argc + 1) * sizeof(char*);
   char** argv_copy;
   char* strings;

   if (!out_argv || !out_block || argc < 0) {
       return -1;
   }

   for (int i = 0; i < argc; i++) {
       const char* arg = (argv && argv[i]) ? argv[i] : "";
       bytes += str_len(arg) + 1;
   }

   argv_copy = (char**)kmalloc(bytes ? bytes : sizeof(char*));
   if (!argv_copy) {
       return -1;
   }

   strings = (char*)(void*)((uint8_t*)argv_copy + (size_t)(argc + 1) * sizeof(char*));
   for (int i = 0; i < argc; i++) {
       const char* arg = (argv && argv[i]) ? argv[i] : "";
       size_t len = str_len(arg) + 1;
       argv_copy[i] = strings;
       str_copy(strings, arg, len);
       strings += len;
   }
   argv_copy[argc] = NULL;

   *out_argv = argv_copy;
   *out_block = argv_copy;
   return 0;
}

int build_user_argv(sched_proc_t* proc) {
   char** user_argv;
   uint64_t ptr_bytes;
   uint64_t cursor;
   uint64_t end;

   if (!proc || proc->argc < 0) {
       return -1;
   }

   ptr_bytes = (uint64_t)(proc->argc + 1) * sizeof(uint64_t);
   if (ptr_bytes >= proc->user_argv_size) {
       return -1;
   }

   user_argv = (char**)(uintptr_t)proc->user_argv_base;
   cursor = proc->user_argv_base + ptr_bytes;
   end = proc->user_argv_base + proc->user_argv_size;

   for (int i = 0; i < proc->argc; i++) {
       const char* arg = proc->argv ? proc->argv[i] : "";
       uint64_t len = str_len(arg) + 1;
       if (cursor + len > end) {
           return -1;
       }

       user_argv[i] = (char*)(uintptr_t)cursor;
       mem_copy((void*)(uintptr_t)cursor, arg, (size_t)len);
       cursor += len;
   }
   user_argv[proc->argc] = NULL;
   proc->user_argv_ptr = proc->user_argv_base;
   return 0;
}

typedef struct {
   uint64_t at_phdr;
   uint64_t at_phent;
   uint64_t at_phnum;
   uint64_t at_base;
   uint64_t at_entry;
   uint64_t at_execfn_ptr;
   uint64_t at_random_ptr;
   uint64_t at_clktck;
} linux_stack_aux_t;

static int build_user_linux_stack(sched_proc_t* proc, uint64_t stack_top, const linux_stack_aux_t* aux) {
   uint64_t arg_ptrs[SCHED_LINUX_ARGV_MAX];
   uint64_t cursor;
   uint64_t stack_min;
   uint64_t random_pos;
   uint64_t execfn_ptr;
   uint64_t seed;
   int argc;

   if (!proc || !aux || proc->argc < 0 || (uint32_t)proc->argc > SCHED_LINUX_ARGV_MAX) {
       return -1;
   }

   argc = proc->argc;
   cursor = stack_top;
   stack_min = proc->user_stack_base;

   for (int i = argc - 1; i >= 0; i--) {
       const char* arg = (proc->argv && proc->argv[i]) ? proc->argv[i] : "";
       uint64_t len = str_len(arg) + 1u;
       if (cursor < stack_min + len) {
           return -1;
       }
       cursor -= len;
       mem_copy((void*)(uintptr_t)cursor, arg, (size_t)len);
       arg_ptrs[i] = cursor;
   }

   if (cursor < stack_min + 16u) {
       return -1;
   }
   cursor -= 16u;
   random_pos = cursor;
   seed = timer_ticks() ^ ((uint64_t)proc->pid << 32);
   for (uint32_t i = 0u; i < 16u; i++) {
       seed = seed * 6364136223846793005ull + 1ull;
       ((uint8_t*)(uintptr_t)random_pos)[i] = (uint8_t)(seed >> 40);
   }

   cursor &= ~0xFULL;
   execfn_ptr = aux->at_execfn_ptr;
   if (execfn_ptr == 0u && argc > 0) {
       execfn_ptr = arg_ptrs[0];
   }

#define LINUX_STACK_PUSH(value_expr)                       \
   do {                                                   \
       if (cursor < stack_min + sizeof(uint64_t)) {      \
           return -1;                                     \
       }                                                  \
       cursor -= sizeof(uint64_t);                        \
       *((uint64_t*)(uintptr_t)cursor) = (value_expr);    \
   } while (0)

   /* Rich auxv for Linux userspace startup/runtime. */
   LINUX_STACK_PUSH(0u);
   LINUX_STACK_PUSH(LINUX_AT_NULL);
   LINUX_STACK_PUSH(execfn_ptr);
   LINUX_STACK_PUSH(LINUX_AT_EXECFN);
   LINUX_STACK_PUSH(aux->at_random_ptr ? aux->at_random_ptr : random_pos);
   LINUX_STACK_PUSH(LINUX_AT_RANDOM);
   LINUX_STACK_PUSH(0u);
   LINUX_STACK_PUSH(LINUX_AT_SECURE);
   LINUX_STACK_PUSH(aux->at_clktck);
   LINUX_STACK_PUSH(LINUX_AT_CLKTCK);
   LINUX_STACK_PUSH(0u);
   LINUX_STACK_PUSH(LINUX_AT_EGID);
   LINUX_STACK_PUSH(0u);
   LINUX_STACK_PUSH(LINUX_AT_GID);
   LINUX_STACK_PUSH((uint64_t)proc->uid);
   LINUX_STACK_PUSH(LINUX_AT_EUID);
   LINUX_STACK_PUSH((uint64_t)proc->uid);
   LINUX_STACK_PUSH(LINUX_AT_UID);
   LINUX_STACK_PUSH(aux->at_entry);
   LINUX_STACK_PUSH(LINUX_AT_ENTRY);
   LINUX_STACK_PUSH(0u);
   LINUX_STACK_PUSH(LINUX_AT_FLAGS);
   LINUX_STACK_PUSH(aux->at_base);
   LINUX_STACK_PUSH(LINUX_AT_BASE);
   LINUX_STACK_PUSH(PAGE_SIZE);
   LINUX_STACK_PUSH(LINUX_AT_PAGESZ);
   LINUX_STACK_PUSH(aux->at_phnum);
   LINUX_STACK_PUSH(LINUX_AT_PHNUM);
   LINUX_STACK_PUSH(aux->at_phent);
   LINUX_STACK_PUSH(LINUX_AT_PHENT);
   LINUX_STACK_PUSH(aux->at_phdr);
   LINUX_STACK_PUSH(LINUX_AT_PHDR);

   /* envp[] = { NULL } */
   LINUX_STACK_PUSH(0u);

   /* argv[argc] = NULL */
   LINUX_STACK_PUSH(0u);
   for (int i = argc - 1; i >= 0; i--) {
       LINUX_STACK_PUSH(arg_ptrs[i]);
   }

   proc->user_argv_ptr = cursor;
   LINUX_STACK_PUSH((uint64_t)(uint32_t)argc);

#undef LINUX_STACK_PUSH

   proc->user_rsp = cursor;
   return 0;
}

int setup_user_process_image(
   sched_proc_t* proc,
   uint32_t slot,
   const elf_image_t* image,
   const elf_image_t* interp_image
) {
   uint64_t base;
   uint64_t image_offset;
   uint64_t main_size;
   uint64_t total_image_size;
   uint64_t interp_offset = 0u;
   uint64_t interp_size = 0u;
   uint64_t main_entry_off;
   uint64_t main_entry;
   uint64_t interp_entry = 0u;
   uint64_t phdr_off = 0u;
   uint64_t linux_clktck = 100u;
   uint64_t stack_top;
   uint64_t prev_cr3;
   uint8_t has_interp = 0u;
   linux_stack_aux_t aux;
   uint8_t trampoline[11] = {
       0x89, 0xC7,
       0xB8, 0x00, 0x00, 0x00, 0x00,
       0xCD, 0x80,
       0xEB, 0xFE,
   };

   if (!proc || !image || !image->load_base || !image->entry) {
       return -1;
   }

   if (proc->linux_compat && interp_image && interp_image->load_base && interp_image->load_size > 0u) {
       has_interp = 1u;
   }

   base = slot_user_region_base(slot);
   image_offset = USER_IMAGE_BASE_OFFSET;
   main_size = align_up(image->load_size, PAGE_SIZE);
   if (main_size == 0) {
       return -1;
   }
   total_image_size = main_size;
   if (has_interp) {
       interp_size = align_up(interp_image->load_size, PAGE_SIZE);
       if (interp_size == 0u) {
           return -1;
       }
       interp_offset = align_up(image_offset + main_size + PAGE_SIZE, PAGE_SIZE);
       if (interp_offset < image_offset || interp_offset + interp_size < interp_offset) {
           return -1;
       }
       total_image_size = (interp_offset - image_offset) + interp_size;
   }
   if (image_offset + total_image_size >= USER_ARGV_BASE_OFFSET) {
       return -1;
   }
   if (proc->page_table_cr3 == 0u) {
       return -1;
   }

   proc->user_mode = 1;
   proc->thread_mode = 0u;
   proc->user_image_base = base + image_offset;
   proc->user_image_size = total_image_size;
   proc->user_trampoline = base + USER_TRAMPOLINE_OFFSET;
   if (proc->linux_compat) {
       proc->user_argv_base = 0u;
       proc->user_argv_size = 0u;
   } else {
       proc->user_argv_base = base + USER_ARGV_BASE_OFFSET;
       proc->user_argv_size = USER_ARGV_SIZE;
   }
   proc->user_stack_base = base + USER_STACK_TOP_OFFSET - USER_STACK_SIZE;
   proc->user_stack_size = USER_STACK_SIZE;
   proc->user_region_base = base;
   proc->user_region_end = base + USER_REGION_STRIDE;
   proc->user_mmap_next = base + USER_MMAP_MIN_OFFSET;
   proc->user_fs_base = 0u;
   proc->linux_brk_base = align_up(proc->user_image_base + total_image_size, PAGE_SIZE);
   proc->linux_brk_current = proc->linux_brk_base;
   proc->linux_brk_limit = proc->linux_brk_base;
   mem_zero(proc->user_maps, sizeof(proc->user_maps));
   mem_zero(&aux, sizeof(aux));

   prev_cr3 = paging_current_cr3();
   if (paging_switch_to(proc->page_table_cr3) != 0) {
       free_user_space(proc);
       return -1;
   }

   if (paging_alloc_user_range(proc->user_image_base, proc->user_image_size, 1u) != 0) {
       (void)paging_switch_to(prev_cr3);
       free_user_space(proc);
       return -1;
   }
   mem_copy((void*)(uintptr_t)proc->user_image_base, image->load_base, (size_t)image->load_size);
   if (has_interp) {
       uint64_t interp_base = base + interp_offset;
       mem_copy((void*)(uintptr_t)interp_base, interp_image->load_base, (size_t)interp_image->load_size);
   }

   if (paging_alloc_user_range(proc->user_trampoline, PAGE_SIZE, 1u) != 0) {
       (void)paging_switch_to(prev_cr3);
       free_user_space(proc);
       return -1;
   }
   trampoline[3] = (uint8_t)(MYAOS_SYS_PROC_EXIT & 0xFFu);
   trampoline[4] = (uint8_t)((MYAOS_SYS_PROC_EXIT >> 8) & 0xFFu);
   trampoline[5] = (uint8_t)((MYAOS_SYS_PROC_EXIT >> 16) & 0xFFu);
   trampoline[6] = (uint8_t)((MYAOS_SYS_PROC_EXIT >> 24) & 0xFFu);
   mem_copy((void*)(uintptr_t)proc->user_trampoline, trampoline, sizeof(trampoline));

   if (!proc->linux_compat) {
       if (paging_alloc_user_range(proc->user_argv_base, proc->user_argv_size, 1u) != 0) {
           (void)paging_switch_to(prev_cr3);
           free_user_space(proc);
           return -1;
       }
       if (build_user_argv(proc) != 0) {
           (void)paging_switch_to(prev_cr3);
           free_user_space(proc);
           return -1;
       }
   }

   if (paging_alloc_user_range(proc->user_stack_base, proc->user_stack_size, 1u) != 0) {
       (void)paging_switch_to(prev_cr3);
       free_user_space(proc);
       return -1;
   }

   if (image->entry_vaddr < image->vaddr_base) {
       (void)paging_switch_to(prev_cr3);
       free_user_space(proc);
       return -1;
   }
   main_entry_off = image->entry_vaddr - image->vaddr_base;
   if (main_entry_off >= image->load_size) {
       (void)paging_switch_to(prev_cr3);
       free_user_space(proc);
       return -1;
   }
   main_entry = proc->user_image_base + main_entry_off;

   if (has_interp) {
       uint64_t interp_base = base + interp_offset;
       uint64_t interp_entry_off;

       if (interp_image->entry_vaddr < interp_image->vaddr_base) {
           (void)paging_switch_to(prev_cr3);
           free_user_space(proc);
           return -1;
       }
       interp_entry_off = interp_image->entry_vaddr - interp_image->vaddr_base;
       if (interp_entry_off >= interp_image->load_size) {
           (void)paging_switch_to(prev_cr3);
           free_user_space(proc);
           return -1;
       }
       interp_entry = interp_base + interp_entry_off;
   }

   proc->user_entry = has_interp ? interp_entry : main_entry;

   if (image->phdr_vaddr >= image->vaddr_base) {
       phdr_off = image->phdr_vaddr - image->vaddr_base;
       if (phdr_off < image->load_size) {
           aux.at_phdr = proc->user_image_base + phdr_off;
       }
   }
   aux.at_phent = image->phentsize;
   aux.at_phnum = image->phnum;
   aux.at_base = has_interp ? (base + interp_offset) : 0u;
   aux.at_entry = main_entry;
   aux.at_execfn_ptr = 0u;
   aux.at_random_ptr = 0u;
   linux_clktck = timer_hz();
   aux.at_clktck = (linux_clktck == 0u) ? 100u : linux_clktck;

   stack_top = base + USER_STACK_TOP_OFFSET;
   if (proc->linux_compat) {
       if (build_user_linux_stack(proc, stack_top, &aux) != 0) {
           (void)paging_switch_to(prev_cr3);
           free_user_space(proc);
           return -1;
       }
   } else {
       uint64_t initial_rsp = stack_top - sizeof(uint64_t);
       *((uint64_t*)(uintptr_t)initial_rsp) = proc->user_trampoline;
       proc->user_rsp = initial_rsp;
   }
   (void)paging_switch_to(prev_cr3);
   return 0;
}

