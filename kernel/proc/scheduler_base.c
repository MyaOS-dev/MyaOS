#include "scheduler_internal.h"

sched_proc_t g_procs[SCHED_MAX_PROCS];
sched_pipe_t g_pipes[SCHED_MAX_PIPES];
sched_user_t g_users[SCHED_MAX_USERS];
sched_shm_t g_shm[SCHED_MAX_SHM_SEGMENTS];
uint32_t g_proc_count;
uint32_t g_current_slot = SCHED_INVALID_SLOT;
uint32_t g_next_pid = 1;
uint32_t g_slice_ticks = 1;
uint64_t g_last_switch_tick;
uint8_t g_started;
uint16_t g_kernel_cs;
uint16_t g_kernel_ss;
uint16_t g_user_cs;
uint16_t g_user_ss;
volatile resched_mode_t g_resched_mode;

uint64_t read_rflags(void) {
  uint64_t rflags;
  __asm__ __volatile__("pushfq; popq %0" : "=r"(rflags));
  return rflags;
}

void write_msr(uint32_t msr, uint64_t value) {
  uint32_t lo = (uint32_t)(value & 0xFFFFFFFFu);
  uint32_t hi = (uint32_t)(value >> 32);
  __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

uint64_t align_up(uint64_t value, uint64_t align) {
  return (value + align - 1ULL) & ~(align - 1ULL);
}

void mem_zero(void* ptr, size_t size) {
  uint8_t* p = (uint8_t*)ptr;
  for (size_t i = 0; i < size; i++) {
      p[i] = 0;
  }
}

void mem_copy(void* dst, const void* src, size_t size) {
   uint8_t* out = (uint8_t*)dst;
   const uint8_t* in = (const uint8_t*)src;
   for (size_t i = 0; i < size; i++) {
       out[i] = in[i];
   }
}

void str_copy(char* dst, const char* src, size_t dst_size) {
   size_t i = 0;

   if (dst_size == 0) {
       return;
   }

   if (!src) {
       dst[0] = '\0';
       return;
   }

   while (i + 1 < dst_size && src[i]) {
       dst[i] = src[i];
       i++;
   }
   dst[i] = '\0';
}

size_t str_len(const char* s) {
   size_t n = 0;
   while (s && s[n]) {
       n++;
   }
   return n;
}

int str_eq(const char* a, const char* b) {
   size_t i = 0;

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

int str_starts_with(const char* text, const char* prefix) {
   size_t i = 0;

   if (!text || !prefix) {
       return 0;
   }
   while (prefix[i]) {
       if (text[i] != prefix[i]) {
           return 0;
       }
       i++;
   }
   return 1;
}

void basename_copy(char* dst, const char* path, size_t dst_size) {
   const char* last = path;

   if (!path) {
       str_copy(dst, "proc", dst_size);
       return;
   }

   for (size_t i = 0; path[i]; i++) {
       if (path[i] == '/') {
           last = &path[i + 1];
       }
   }

   str_copy(dst, last[0] ? last : "proc", dst_size);
}

int load_linux_interpreter_image(const char* interp_path, elf_image_t* out) {
   char fallback[MYAOS_PATH_MAX];
   size_t pos;

   if (!interp_path || !interp_path[0] || !out) {
       return -1;
   }
   if (elf_load_from_vfs("/", interp_path, out) == 0) {
       return 0;
   }
   if (!str_starts_with(interp_path, "/")) {
       return -1;
   }

   str_copy(fallback, "/boot", sizeof(fallback));
   pos = str_len(fallback);
   for (size_t i = 0u; interp_path[i] && pos + 1u < sizeof(fallback); i++) {
       fallback[pos++] = interp_path[i];
   }
   fallback[pos] = '\0';
   if (fallback[0] == '\0') {
       return -1;
   }
   return elf_load_from_vfs("/", fallback, out);
}

int find_user_slot_by_name(const char* name) {
   if (!name || !name[0]) {
       return -1;
   }
   for (uint32_t i = 0; i < SCHED_MAX_USERS; i++) {
       if (g_users[i].used && g_users[i].name[0]) {
           if (str_eq(g_users[i].name, name)) {
               return (int)i;
           }
       }
   }
   return -1;
}

int find_shm_slot_by_name(const char* name) {
   if (!name || !name[0]) {
       return -1;
   }
   for (uint32_t i = 0; i < SCHED_MAX_SHM_SEGMENTS; i++) {
       if (g_shm[i].used && str_eq(g_shm[i].name, name)) {
           return (int)i;
       }
   }
   return -1;
}

sched_shm_t* shm_from_id(int32_t id) {
   uint32_t slot;

   if (id <= 0) {
       return NULL;
   }
   slot = (uint32_t)(id - 1);
   if (slot >= SCHED_MAX_SHM_SEGMENTS || !g_shm[slot].used) {
       return NULL;
   }
   return &g_shm[slot];
}

uint32_t count_procs_with_cr3(uint64_t cr3) {
   uint32_t count = 0;

   if (cr3 == 0u) {
       return 0u;
   }
   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       if (g_procs[i].used && g_procs[i].page_table_cr3 == cr3) {
           count++;
       }
   }
   return count;
}

void proc_write_i32_ptr(sched_proc_t* proc, int32_t* ptr, int32_t value) {
   uint64_t current_cr3;
   uint64_t prev_cr3;

   if (!proc || !ptr) {
       return;
   }

   current_cr3 = paging_current_cr3();
   if (!proc->user_mode || proc->page_table_cr3 == 0u || proc->page_table_cr3 == current_cr3) {
       *ptr = value;
       return;
   }

   prev_cr3 = current_cr3;
   if (paging_switch_to(proc->page_table_cr3) != 0) {
       return;
   }
   *ptr = value;
   (void)paging_switch_to(prev_cr3);
}

void proc_write_u32_ptr(sched_proc_t* proc, uint32_t* ptr, uint32_t value) {
   uint64_t current_cr3;
   uint64_t prev_cr3;

   if (!proc || !ptr) {
       return;
   }

   current_cr3 = paging_current_cr3();
   if (!proc->user_mode || proc->page_table_cr3 == 0u || proc->page_table_cr3 == current_cr3) {
       *ptr = value;
       return;
   }

   prev_cr3 = current_cr3;
   if (paging_switch_to(proc->page_table_cr3) != 0) {
       return;
   }
   *ptr = value;
   (void)paging_switch_to(prev_cr3);
}

uint64_t slot_user_region_base(uint32_t slot) {
   return USER_REGION_BASE + (uint64_t)slot * USER_REGION_STRIDE;
}

uint8_t ranges_overlap(uint64_t a_start, uint64_t a_end, uint64_t b_start, uint64_t b_end) {
   return (uint8_t)(a_start < b_end && b_start < a_end);
}

uint8_t user_map_is_swapped(const user_map_t* map) {
   return (uint8_t)(map && map->used && (map->reserved0 & USER_MAP_FLAG_SWAPPED) != 0u);
}

uint64_t proc_user_mapped_bytes(const sched_proc_t* proc) {
   uint64_t total = 0u;

   if (!proc) {
       return 0u;
   }

   for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
       if (!proc->user_maps[i].used || proc->user_maps[i].size == 0u) {
           continue;
       }
       total += proc->user_maps[i].size;
   }
   return total;
}

sched_proc_t* current_proc(void) {
   if (g_current_slot >= SCHED_MAX_PROCS || !g_procs[g_current_slot].used) {
       return NULL;
   }
   return &g_procs[g_current_slot];
}

sched_proc_t* find_proc_by_pid(int32_t pid) {
   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       if (g_procs[i].used && (int32_t)g_procs[i].pid == pid) {
           return &g_procs[i];
       }
   }
   return NULL;
}

int find_proc_slot_by_pid(int32_t pid) {
   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       if (g_procs[i].used && (int32_t)g_procs[i].pid == pid) {
           return (int)i;
       }
   }
   return -1;
}

int pipe_slot_valid(uint32_t pipe_slot) {
   return (pipe_slot < SCHED_MAX_PIPES && g_pipes[pipe_slot].used) ? 1 : 0;
}

int alloc_pipe_slot(void) {
   for (uint32_t i = 0; i < SCHED_MAX_PIPES; i++) {
       if (!g_pipes[i].used) {
           g_pipes[i].used = 1u;
           g_pipes[i].readers = 0u;
           g_pipes[i].writers = 0u;
           g_pipes[i].head = 0u;
           g_pipes[i].len = 0u;
           return (int)i;
       }
   }
   return -1;
}

void maybe_release_pipe(uint32_t pipe_slot) {
   if (pipe_slot >= SCHED_MAX_PIPES || !g_pipes[pipe_slot].used) {
       return;
   }
   if (g_pipes[pipe_slot].readers == 0u && g_pipes[pipe_slot].writers == 0u) {
       g_pipes[pipe_slot].used = 0u;
       g_pipes[pipe_slot].head = 0u;
       g_pipes[pipe_slot].len = 0u;
   }
}

int alloc_proc_pipe_handle(sched_proc_t* proc) {
   if (!proc) {
       return -1;
   }
   for (uint32_t i = 0; i < SCHED_MAX_PIPE_HANDLES; i++) {
       if (!proc->pipe_handles[i].used) {
           return (int)i;
       }
   }
   return -1;
}

int close_proc_pipe_handle(sched_proc_t* proc, int fd) {
   uint32_t slot;
   uint8_t mode;
   uint32_t pipe_slot;

   if (!proc || fd <= 0 || fd > (int)SCHED_MAX_PIPE_HANDLES) {
       return -1;
   }
   slot = (uint32_t)(fd - 1);
   if (!proc->pipe_handles[slot].used) {
       return -1;
   }

   mode = proc->pipe_handles[slot].mode;
   pipe_slot = proc->pipe_handles[slot].pipe_slot;
   proc->pipe_handles[slot].used = 0u;
   proc->pipe_handles[slot].mode = 0u;
   proc->pipe_handles[slot].pipe_slot = 0u;

   if (pipe_slot_valid(pipe_slot)) {
       if ((mode & PIPE_MODE_READ) != 0u && g_pipes[pipe_slot].readers > 0u) {
           g_pipes[pipe_slot].readers--;
       }
       if ((mode & PIPE_MODE_WRITE) != 0u && g_pipes[pipe_slot].writers > 0u) {
           g_pipes[pipe_slot].writers--;
       }
       maybe_release_pipe(pipe_slot);
   }
   return 0;
}

void close_all_proc_pipe_handles(sched_proc_t* proc) {
   if (!proc) {
       return;
   }
   for (uint32_t i = 0; i < SCHED_MAX_PIPE_HANDLES; i++) {
       if (proc->pipe_handles[i].used) {
           (void)close_proc_pipe_handle(proc, (int)i + 1);
       }
   }
}

proc_pipe_handle_t* proc_pipe_handle_for_fd(sched_proc_t* proc, int32_t fd, uint8_t required_mode) {
   uint32_t slot;
   proc_pipe_handle_t* handle;

   if (!proc || fd <= 0 || fd > (int32_t)SCHED_MAX_PIPE_HANDLES) {
       return NULL;
   }

   slot = (uint32_t)(fd - 1);
   handle = &proc->pipe_handles[slot];
   if (!handle->used) {
       return NULL;
   }
   if ((handle->mode & required_mode) != required_mode) {
       return NULL;
   }
   if (!pipe_slot_valid(handle->pipe_slot)) {
       return NULL;
   }
   return handle;
}

int alloc_proc_file_handle(sched_proc_t* proc) {
   if (!proc) {
       return -1;
   }
   for (uint32_t i = 0; i < SCHED_MAX_FILE_HANDLES; i++) {
       if (!proc->file_handles[i].used) {
           return (int)i;
       }
   }
   return -1;
}

int ensure_file_capacity(proc_file_handle_t* handle, uint32_t need) {
   uint32_t new_capacity;
   uint8_t* new_data;

   if (!handle) {
       return -1;
   }
   if (need <= handle->capacity) {
       return 0;
   }
   if (need > SCHED_POSIX_FILE_MAX) {
       return -1;
   }

   new_capacity = handle->capacity ? handle->capacity : 256u;
   while (new_capacity < need) {
       if (new_capacity >= SCHED_POSIX_FILE_MAX / 2u) {
           new_capacity = SCHED_POSIX_FILE_MAX;
           break;
       }
       new_capacity *= 2u;
   }
   if (new_capacity < need) {
       return -1;
   }

   new_data = (uint8_t*)kmalloc(new_capacity ? new_capacity : 1u);
   if (!new_data) {
       return -1;
   }
   if (handle->data && handle->size > 0u) {
       mem_copy(new_data, handle->data, handle->size);
   }
   if (handle->data) {
       kfree(handle->data);
   }
   handle->data = new_data;
   handle->capacity = new_capacity;
   return 0;
}

int clone_proc_file_handle(proc_file_handle_t* dst, const proc_file_handle_t* src) {
   if (!dst || !src) {
       return -1;
   }
   mem_zero(dst, sizeof(*dst));
   if (!src->used) {
       return 0;
   }

   dst->used = src->used;
   dst->readable = src->readable;
   dst->writable = src->writable;
   dst->append = src->append;
   dst->dirty = src->dirty;
   dst->size = src->size;
   dst->capacity = src->capacity;
   dst->offset = src->offset;
   str_copy(dst->abs_path, src->abs_path, sizeof(dst->abs_path));

   if (dst->capacity > 0u) {
       dst->data = (uint8_t*)kmalloc(dst->capacity);
       if (!dst->data) {
           mem_zero(dst, sizeof(*dst));
           return -1;
       }
       if (src->size > 0u && src->data) {
           mem_copy(dst->data, src->data, src->size);
       }
   }
   return 0;
}

int close_proc_file_handle(sched_proc_t* proc, int fd) {
   uint32_t slot;
   proc_file_handle_t* handle;
   int rc = 0;

   if (!proc || fd <= 0 || fd > (int)SCHED_MAX_FILE_HANDLES) {
       return -1;
   }
   slot = (uint32_t)(fd - 1);
   handle = &proc->file_handles[slot];
   if (!handle->used) {
       return -1;
   }

   if (handle->dirty && handle->writable) {
       rc = vfs_write_file("/", handle->abs_path, handle->data, handle->size);
   }
   if (handle->data) {
       kfree(handle->data);
   }
   mem_zero(handle, sizeof(*handle));
   return rc;
}

void close_all_proc_file_handles(sched_proc_t* proc) {
   if (!proc) {
       return;
   }
   for (uint32_t i = 0; i < SCHED_MAX_FILE_HANDLES; i++) {
       if (proc->file_handles[i].used) {
           (void)close_proc_file_handle(proc, (int)i + 1);
       }
   }
}

proc_file_handle_t* proc_file_handle_for_fd(sched_proc_t* proc, int32_t fd, uint8_t required_mode) {
   uint32_t slot;
   proc_file_handle_t* handle;
   uint8_t mode = 0u;

   if (!proc || fd <= 0 || fd > (int32_t)SCHED_MAX_FILE_HANDLES) {
       return NULL;
   }
   slot = (uint32_t)(fd - 1);
   handle = &proc->file_handles[slot];
   if (!handle->used) {
       return NULL;
   }

   if (handle->readable) {
       mode |= FILE_MODE_READ;
   }
   if (handle->writable) {
       mode |= FILE_MODE_WRITE;
   }
   if ((mode & required_mode) != required_mode) {
       return NULL;
   }
   return handle;
}

int slot_is_runnable(uint32_t slot) {
   return slot < SCHED_MAX_PROCS && g_procs[slot].used && g_procs[slot].state == MYAOS_PROC_READY;
}

uint8_t sanitize_priority(uint8_t priority) {
   if (priority < MYAOS_PROC_PRIO_LOW || priority > MYAOS_PROC_PRIO_HIGH) {
       return MYAOS_PROC_PRIO_NORMAL;
   }
   return priority;
}

uint8_t priority_budget(uint8_t priority) {
   priority = sanitize_priority(priority);
   if (priority == MYAOS_PROC_PRIO_HIGH) {
       return SCHED_PRIO_BUDGET_HIGH;
   }
   if (priority == MYAOS_PROC_PRIO_NORMAL) {
       return SCHED_PRIO_BUDGET_NORMAL;
   }
   return SCHED_PRIO_BUDGET_LOW;
}

void replenish_ready_budgets(void) {
   for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
       if (!slot_is_runnable(i)) {
           continue;
       }
       g_procs[i].sched_budget = priority_budget(g_procs[i].priority);
   }
}

uint32_t find_next_ready_with_budget(uint32_t from) {
   uint32_t best_slot = SCHED_INVALID_SLOT;
   uint8_t best_priority = 0;

   if (g_proc_count == 0) {
       return SCHED_INVALID_SLOT;
   }

   for (uint32_t step = 1; step <= SCHED_MAX_PROCS; step++) {
       uint32_t slot = (from + step) % SCHED_MAX_PROCS;
       uint8_t priority;

       if (!slot_is_runnable(slot)) {
           continue;
       }
       if (g_procs[slot].sched_budget == 0u) {
           continue;
       }

       priority = sanitize_priority(g_procs[slot].priority);
       if (best_slot == SCHED_INVALID_SLOT || priority > best_priority) {
           best_slot = slot;
           best_priority = priority;
       }
   }

   return best_slot;
}

uint32_t find_next_ready(uint32_t from) {
   uint32_t slot = find_next_ready_with_budget(from);

   if (slot != SCHED_INVALID_SLOT) {
       return slot;
   }

   replenish_ready_budgets();
   return find_next_ready_with_budget(from);
}

uint32_t first_ready_slot(void) {
   return find_next_ready(SCHED_MAX_PROCS - 1u);
}

void load_proc_fs_base(const sched_proc_t* proc) {
   uint64_t fs_base = 0u;

   if (proc && proc->used && proc->user_mode && proc->linux_compat) {
       fs_base = proc->user_fs_base;
   }
   write_msr(IA32_FS_BASE_MSR, fs_base);
}

void clear_user_meta(sched_proc_t* proc) {
   if (!proc) {
       return;
   }

   proc->user_mode = 0;
   proc->thread_mode = 0;
   proc->thread_arg = 0u;
   proc->user_image_base = 0u;
   proc->user_image_size = 0u;
   proc->user_argv_base = 0u;
   proc->user_argv_size = 0u;
   proc->user_stack_base = 0u;
   proc->user_stack_size = 0u;
   proc->user_trampoline = 0u;
   proc->user_entry = 0u;
   proc->user_rsp = 0u;
   proc->user_argv_ptr = 0u;
   proc->user_region_base = 0u;
   proc->user_region_end = 0u;
   proc->user_mmap_next = 0u;
   proc->user_fs_base = 0u;
   proc->linux_brk_base = 0u;
   proc->linux_brk_current = 0u;
   proc->linux_brk_limit = 0u;
   mem_zero(proc->user_maps, sizeof(proc->user_maps));
}

void free_user_space(sched_proc_t* proc) {
   uint64_t prev_cr3;

   if (!proc) {
       return;
   }
   if (proc->page_table_cr3 == 0u) {
       goto clear_meta;
   }

   prev_cr3 = paging_current_cr3();
   if (paging_switch_to(proc->page_table_cr3) != 0) {
       goto clear_meta;
   }

   for (uint32_t i = 0; i < SCHED_MAX_USER_MAPS; i++) {
       if (!proc->user_maps[i].used || proc->user_maps[i].size == 0u) {
           continue;
       }
       if (user_map_is_swapped(&proc->user_maps[i])) {
           (void)swap_free(proc->user_maps[i].reserved1);
       } else {
           paging_free_user_range(proc->user_maps[i].base, proc->user_maps[i].size);
       }
   }
   if (proc->user_image_size) {
       paging_free_user_range(proc->user_image_base, proc->user_image_size);
   }
   if (proc->user_argv_size) {
       paging_free_user_range(proc->user_argv_base, proc->user_argv_size);
   }
   if (proc->user_stack_size) {
       paging_free_user_range(proc->user_stack_base, proc->user_stack_size);
   }
   if (proc->linux_brk_limit > proc->linux_brk_base) {
       paging_free_user_range(proc->linux_brk_base, proc->linux_brk_limit - proc->linux_brk_base);
   }
   if (proc->user_trampoline) {
       paging_free_user_range(proc->user_trampoline, PAGE_SIZE);
   }

   (void)paging_switch_to(prev_cr3);

clear_meta:
   clear_user_meta(proc);
}

int clone_user_range_between_spaces(
   uint64_t src_cr3,
   uint64_t dst_cr3,
   uint64_t base,
   uint64_t size,
   uint8_t writable,
   uint8_t* scratch_page
) {
   uint64_t start;
   uint64_t end;

   if (size == 0u) {
       return 0;
   }
   if (base == 0u || !scratch_page || base + size < base) {
       return -1;
   }

   start = base & ~(PAGE_SIZE - 1ULL);
   end = align_up(base + size, PAGE_SIZE);
   if (end <= start) {
       return -1;
   }

   if (paging_switch_to(src_cr3) != 0) {
       return -1;
   }
   if (!paging_user_range_accessible(start, end - start, 0u)) {
       return -1;
   }
   if (paging_switch_to(dst_cr3) != 0) {
       return -1;
   }
   if (paging_alloc_user_range(start, end - start, writable ? 1u : 0u) != 0) {
       return -1;
   }

   for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
       if (paging_switch_to(src_cr3) != 0) {
           return -1;
       }
       mem_copy(scratch_page, (const void*)(uintptr_t)addr, PAGE_SIZE);
       if (paging_switch_to(dst_cr3) != 0) {
           return -1;
       }
       mem_copy((void*)(uintptr_t)addr, scratch_page, PAGE_SIZE);
   }

   return 0;
}

int clone_process_user_space(const sched_proc_t* parent, sched_proc_t* child) {
   uint64_t prev_cr3;
   uint64_t child_cr3 = 0u;
   uint8_t* scratch_page = NULL;
   uint64_t brk_size = 0u;
   int rc = -1;

   if (!parent || !child || parent->page_table_cr3 == 0u || parent->page_table_cr3 == paging_kernel_cr3()) {
       return -1;
   }

   prev_cr3 = paging_current_cr3();
   scratch_page = (uint8_t*)kmalloc(PAGE_SIZE);
   if (!scratch_page) {
       return -1;
   }

   if (paging_space_create(&child_cr3) != 0) {
       goto done;
   }

   if (clone_user_range_between_spaces(
           parent->page_table_cr3,
           child_cr3,
           parent->user_image_base,
           parent->user_image_size,
           1u,
           scratch_page
       ) != 0) {
       goto done;
   }
   if (clone_user_range_between_spaces(
           parent->page_table_cr3,
           child_cr3,
           parent->user_trampoline,
           PAGE_SIZE,
           1u,
           scratch_page
       ) != 0) {
       goto done;
   }
   if (clone_user_range_between_spaces(
           parent->page_table_cr3,
           child_cr3,
           parent->user_argv_base,
           parent->user_argv_size,
           1u,
           scratch_page
       ) != 0) {
       goto done;
   }
   if (clone_user_range_between_spaces(
           parent->page_table_cr3,
           child_cr3,
           parent->user_stack_base,
           parent->user_stack_size,
           1u,
           scratch_page
       ) != 0) {
       goto done;
   }

   if (parent->linux_brk_limit > parent->linux_brk_base) {
       brk_size = parent->linux_brk_limit - parent->linux_brk_base;
   }
   if (clone_user_range_between_spaces(
           parent->page_table_cr3,
           child_cr3,
           parent->linux_brk_base,
           brk_size,
           1u,
           scratch_page
       ) != 0) {
       goto done;
   }

   for (uint32_t i = 0u; i < SCHED_MAX_USER_MAPS; i++) {
       const user_map_t* map = &parent->user_maps[i];
       if (!map->used || map->size == 0u) {
           continue;
       }
       if (user_map_is_swapped(map)) {
           goto done;
       }
       if (clone_user_range_between_spaces(
               parent->page_table_cr3,
               child_cr3,
               map->base,
               map->size,
               /* We need writable pages during bootstrap copy; effective
                  per-map permissions are tracked in user_map metadata and
                  can be tightened later with real mprotect support. */
               1u,
               scratch_page
           ) != 0) {
           goto done;
       }
   }

   child->page_table_cr3 = child_cr3;
   child_cr3 = 0u;
   rc = 0;

done:
   if (paging_switch_to(prev_cr3) != 0) {
       rc = -1;
   }
   if (child_cr3 != 0u) {
       paging_space_destroy(child_cr3);
   }
   if (scratch_page) {
       kfree(scratch_page);
   }
   return rc;
}
