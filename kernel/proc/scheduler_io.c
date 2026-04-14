#include "scheduler_internal.h"

int scheduler_notify(int32_t pid, uint32_t bits) {
    sched_proc_t* proc = find_proc_by_pid(pid);

    if (!proc || bits == 0) {
        return -1;
    }

    proc->notify_bits |= bits;
    maybe_wake_notify_waiter(proc);
    return 0;
}

void scheduler_notify_all(uint32_t bits) {
    if (bits == 0u) {
        return;
    }

    for (uint32_t i = 0; i < SCHED_MAX_PROCS; i++) {
        sched_proc_t* proc = &g_procs[i];
        if (!proc->used || proc->state == MYAOS_PROC_ZOMBIE) {
            continue;
        }
        proc->notify_bits |= bits;
        maybe_wake_notify_waiter(proc);
    }
}

int scheduler_notify_poll(uint32_t mask, uint8_t clear, uint32_t* out_bits) {
    sched_proc_t* proc = current_proc();
    uint32_t effective_mask = mask ? mask : 0xFFFFFFFFu;
    uint32_t bits;

    if (!proc || !out_bits) {
        return -1;
    }

    bits = proc->notify_bits & effective_mask;
    *out_bits = bits;
    if (bits == 0) {
        return -1;
    }
    if (clear) {
        proc->notify_bits &= ~bits;
    }
    return 0;
}

int scheduler_notify_wait(uint32_t mask, uint32_t timeout_ticks, uint8_t clear, uint32_t* out_bits) {
    sched_proc_t* proc = current_proc();
    uint32_t effective_mask = mask ? mask : 0xFFFFFFFFu;
    uint32_t bits;
    uint64_t now;
    uint64_t deadline;

    if (!proc || !out_bits) {
        return -1;
    }

    bits = proc->notify_bits & effective_mask;
    if (bits != 0u) {
        *out_bits = bits;
        if (clear) {
            proc->notify_bits &= ~bits;
        }
        return 0;
    }
    if (timeout_ticks == 0u) {
        *out_bits = 0u;
        return -1;
    }

    now = timer_ticks();
    deadline = now + (uint64_t)timeout_ticks;
    if (deadline < now) {
        deadline = UINT64_MAX;
    }

    *out_bits = 0u;
    proc->wait_exit_ptr = NULL;
    proc->wait_pid = SCHED_WAIT_NOTIFY_SENTINEL;
    proc->wait_notify_active = 1u;
    proc->wait_notify_clear = clear ? 1u : 0u;
    proc->wait_notify_reserved0 = 0u;
    proc->wait_notify_mask = mask;
    proc->wait_notify_out_ptr = out_bits;
    proc->wait_notify_deadline = deadline;
    proc->wait_vfork = 0u;
    proc->state = MYAOS_PROC_BLOCKED;
    g_resched_mode = RESCHED_SAVE;
    return 0;
}

int scheduler_ipc_send(int32_t pid, const char* data, uint32_t len) {
    sched_proc_t* proc = find_proc_by_pid(pid);
    sched_proc_t* sender = current_proc();
    uint32_t copy_len;

    if (!proc || !data || len == 0) {
        return -1;
    }
    if (proc->ipc_full) {
        return -1;
    }

    copy_len = (len >= MYAOS_IPC_MSG_MAX) ? (MYAOS_IPC_MSG_MAX - 1u) : len;
    for (uint32_t i = 0; i < copy_len; i++) {
        proc->ipc_data[i] = data[i];
    }
    proc->ipc_data[copy_len] = '\0';
    proc->ipc_len = copy_len;
    proc->ipc_from = sender ? sender->pid : 0u;
    proc->ipc_full = 1;
    proc->notify_bits |= 0x1u;
    maybe_wake_notify_waiter(proc);
    return 0;
}

int scheduler_ipc_recv(char* out_buf, uint32_t max_len, uint32_t* out_len, int32_t* out_from) {
    sched_proc_t* proc = current_proc();
    uint32_t copy_len;

    if (!proc || !out_buf || max_len == 0 || !out_len) {
        return -1;
    }
    if (!proc->ipc_full) {
        return -1;
    }

    copy_len = proc->ipc_len;
    if (copy_len + 1 > max_len) {
        copy_len = max_len - 1u;
    }

    for (uint32_t i = 0; i < copy_len; i++) {
        out_buf[i] = proc->ipc_data[i];
    }
    out_buf[copy_len] = '\0';
    *out_len = copy_len;
    if (out_from) {
        *out_from = (int32_t)proc->ipc_from;
    }

    proc->ipc_full = 0;
    proc->ipc_len = 0;
    proc->ipc_from = 0;
    proc->ipc_data[0] = '\0';
    proc->notify_bits &= ~0x1u;
    return 0;
}

int scheduler_pipe_create(int32_t* out_read_fd, int32_t* out_write_fd) {
    sched_proc_t* proc = current_proc();
    int pipe_slot;
    int read_slot;
    int write_slot;

    if (!proc || !out_read_fd || !out_write_fd) {
        return -1;
    }

    pipe_slot = alloc_pipe_slot();
    if (pipe_slot < 0) {
        return -1;
    }

    read_slot = alloc_proc_pipe_handle(proc);
    if (read_slot < 0) {
        g_pipes[pipe_slot].used = 0u;
        return -1;
    }
    proc->pipe_handles[read_slot].used = 1u;
    proc->pipe_handles[read_slot].mode = PIPE_MODE_READ;
    proc->pipe_handles[read_slot].reserved0 = 0u;
    proc->pipe_handles[read_slot].pipe_slot = (uint32_t)pipe_slot;

    write_slot = alloc_proc_pipe_handle(proc);
    if (write_slot < 0) {
        proc->pipe_handles[read_slot].used = 0u;
        proc->pipe_handles[read_slot].mode = 0u;
        proc->pipe_handles[read_slot].pipe_slot = 0u;
        g_pipes[pipe_slot].used = 0u;
        return -1;
    }
    proc->pipe_handles[write_slot].used = 1u;
    proc->pipe_handles[write_slot].mode = PIPE_MODE_WRITE;
    proc->pipe_handles[write_slot].reserved0 = 0u;
    proc->pipe_handles[write_slot].pipe_slot = (uint32_t)pipe_slot;

    g_pipes[pipe_slot].readers = 1u;
    g_pipes[pipe_slot].writers = 1u;
    *out_read_fd = read_slot + 1;
    *out_write_fd = write_slot + 1;
    return 0;
}

int scheduler_pipe_close(int32_t fd) {
    sched_proc_t* proc = current_proc();
    return close_proc_pipe_handle(proc, (int)fd);
}

int scheduler_pipe_read(int32_t fd, void* out_buf, uint32_t max_len, uint32_t* out_read) {
    sched_proc_t* proc = current_proc();
    proc_pipe_handle_t* handle;
    sched_pipe_t* pipe;
    uint8_t* out = (uint8_t*)out_buf;
    uint32_t copy_len;
    uint32_t tail;

    if (!proc || !out_read || (max_len != 0u && !out)) {
        return -1;
    }

    handle = proc_pipe_handle_for_fd(proc, fd, PIPE_MODE_READ);
    if (!handle) {
        return -1;
    }
    pipe = &g_pipes[handle->pipe_slot];

    if (max_len == 0u || pipe->len == 0u) {
        *out_read = 0u;
        return 0;
    }

    copy_len = (pipe->len < max_len) ? pipe->len : max_len;
    tail = (pipe->head + SCHED_PIPE_BUFFER_SIZE - pipe->len) % SCHED_PIPE_BUFFER_SIZE;
    {
        uint32_t first = SCHED_PIPE_BUFFER_SIZE - tail;
        uint32_t second;
        if (first > copy_len) {
            first = copy_len;
        }
        second = copy_len - first;
        if (first > 0u) {
            mem_copy(out, &pipe->data[tail], first);
        }
        if (second > 0u) {
            mem_copy(out + first, &pipe->data[0], second);
        }
    }
    pipe->len -= copy_len;
    *out_read = copy_len;
    return 0;
}

int scheduler_pipe_write(int32_t fd, const void* data, uint32_t len, uint32_t* out_written) {
    sched_proc_t* proc = current_proc();
    proc_pipe_handle_t* handle;
    sched_pipe_t* pipe;
    const uint8_t* in = (const uint8_t*)data;
    uint32_t space;
    uint32_t copy_len;

    if (!proc || !out_written || (len != 0u && !in)) {
        return -1;
    }

    handle = proc_pipe_handle_for_fd(proc, fd, PIPE_MODE_WRITE);
    if (!handle) {
        return -1;
    }
    pipe = &g_pipes[handle->pipe_slot];
    if (pipe->readers == 0u) {
        return -1;
    }

    space = SCHED_PIPE_BUFFER_SIZE - pipe->len;
    copy_len = (len < space) ? len : space;
    {
        uint32_t first = SCHED_PIPE_BUFFER_SIZE - pipe->head;
        uint32_t second;
        if (first > copy_len) {
            first = copy_len;
        }
        second = copy_len - first;
        if (first > 0u) {
            mem_copy(&pipe->data[pipe->head], in, first);
        }
        if (second > 0u) {
            mem_copy(&pipe->data[0], in + first, second);
        }
    }
    pipe->head = (pipe->head + copy_len) % SCHED_PIPE_BUFFER_SIZE;
    pipe->len += copy_len;
    *out_written = copy_len;
    return 0;
}

int scheduler_posix_open(const char* path, uint32_t flags, int32_t* out_fd) {
    sched_proc_t* proc = current_proc();
    proc_file_handle_t* handle;
    uint8_t* read_buf;
    uint32_t read_size = 0u;
    uint8_t acc_mode;
    uint8_t create;
    uint8_t trunc;
    char abs_path[MYAOS_PATH_MAX];
    int slot;

    if (!proc || !path || !out_fd) {
        return -1;
    }

    acc_mode = (uint8_t)(flags & MYAOS_POSIX_O_ACCMODE);
    if (acc_mode > MYAOS_POSIX_O_RDWR) {
        return -1;
    }
    create = (uint8_t)((flags & MYAOS_POSIX_O_CREAT) != 0u);
    trunc = (uint8_t)((flags & MYAOS_POSIX_O_TRUNC) != 0u);
    if (trunc && acc_mode == MYAOS_POSIX_O_RDONLY) {
        return -1;
    }

    if (vfs_resolve_cwd(proc->cwd, path, abs_path, sizeof(abs_path)) != 0) {
        return -1;
    }
    if (vfs_is_dir("/", abs_path) > 0) {
        return -1;
    }
    if (create && vfs_touch("/", abs_path) != 0) {
        return -1;
    }

    read_buf = (uint8_t*)kmalloc(SCHED_POSIX_FILE_MAX ? SCHED_POSIX_FILE_MAX : 1u);
    if (!read_buf) {
        return -1;
    }
    if (vfs_read_file("/", abs_path, read_buf, SCHED_POSIX_FILE_MAX, &read_size) != 0) {
        kfree(read_buf);
        return -1;
    }
    if (trunc) {
        read_size = 0u;
    }

    slot = alloc_proc_file_handle(proc);
    if (slot < 0) {
        kfree(read_buf);
        return -1;
    }
    handle = &proc->file_handles[slot];
    mem_zero(handle, sizeof(*handle));
    handle->used = 1u;
    handle->readable = (uint8_t)(acc_mode != MYAOS_POSIX_O_WRONLY);
    handle->writable = (uint8_t)(acc_mode != MYAOS_POSIX_O_RDONLY);
    handle->append = (uint8_t)((flags & MYAOS_POSIX_O_APPEND) != 0u);
    handle->dirty = 0u;
    handle->size = 0u;
    handle->capacity = 0u;
    handle->offset = 0u;
    handle->data = NULL;
    str_copy(handle->abs_path, abs_path, sizeof(handle->abs_path));

    if (read_size > 0u) {
        if (ensure_file_capacity(handle, read_size) != 0) {
            mem_zero(handle, sizeof(*handle));
            kfree(read_buf);
            return -1;
        }
        mem_copy(handle->data, read_buf, read_size);
        handle->size = read_size;
    }
    handle->offset = handle->append ? handle->size : 0u;

    if (trunc) {
        uint8_t empty = 0u;
        handle->dirty = 1u;
        if (vfs_write_file("/", handle->abs_path, &empty, 0u) != 0) {
            (void)close_proc_file_handle(proc, slot + 1);
            kfree(read_buf);
            return -1;
        }
        handle->dirty = 0u;
    }

    *out_fd = slot + 1;
    kfree(read_buf);
    return 0;
}

int scheduler_posix_close(int32_t fd) {
    sched_proc_t* proc = current_proc();
    return close_proc_file_handle(proc, (int)fd);
}

int scheduler_posix_read(int32_t fd, void* out_buf, uint32_t max_len, uint32_t* out_read) {
    sched_proc_t* proc = current_proc();
    proc_file_handle_t* handle;
    uint8_t* out = (uint8_t*)out_buf;
    uint32_t copy_len;

    if (!proc || !out_read || (max_len != 0u && !out)) {
        return -1;
    }

    handle = proc_file_handle_for_fd(proc, fd, FILE_MODE_READ);
    if (!handle) {
        return -1;
    }
    if (max_len == 0u || handle->offset >= handle->size) {
        *out_read = 0u;
        return 0;
    }

    copy_len = handle->size - handle->offset;
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    mem_copy(out, handle->data + handle->offset, copy_len);
    handle->offset += copy_len;
    *out_read = copy_len;
    return 0;
}

int scheduler_posix_write(int32_t fd, const void* data, uint32_t len, uint32_t* out_written) {
    sched_proc_t* proc = current_proc();
    proc_file_handle_t* handle;
    const uint8_t* in = (const uint8_t*)data;
    uint32_t write_pos;
    uint32_t new_end;

    if (!proc || !out_written || (len != 0u && !in)) {
        return -1;
    }

    handle = proc_file_handle_for_fd(proc, fd, FILE_MODE_WRITE);
    if (!handle) {
        return -1;
    }
    if (len == 0u) {
        *out_written = 0u;
        return 0;
    }

    write_pos = handle->append ? handle->size : handle->offset;
    if (write_pos > SCHED_POSIX_FILE_MAX || len > SCHED_POSIX_FILE_MAX || write_pos + len < write_pos ||
        write_pos + len > SCHED_POSIX_FILE_MAX) {
        return -1;
    }
    new_end = write_pos + len;
    if (ensure_file_capacity(handle, new_end) != 0) {
        return -1;
    }

    mem_copy(handle->data + write_pos, in, len);
    handle->offset = new_end;
    if (new_end > handle->size) {
        handle->size = new_end;
    }
    handle->dirty = 1u;
    if (vfs_write_file("/", handle->abs_path, handle->data, handle->size) != 0) {
        return -1;
    }
    handle->dirty = 0u;
    *out_written = len;
    return 0;
}

int scheduler_posix_lseek(int32_t fd, int64_t offset, uint32_t whence, uint64_t* out_offset) {
    sched_proc_t* proc = current_proc();
    proc_file_handle_t* handle;
    int64_t base;
    int64_t next;

    if (!proc) {
        return -1;
    }

    handle = proc_file_handle_for_fd(proc, fd, 0u);
    if (!handle) {
        return -1;
    }

    switch (whence) {
    case MYAOS_POSIX_SEEK_SET:
        base = 0;
        break;
    case MYAOS_POSIX_SEEK_CUR:
        base = (int64_t)handle->offset;
        break;
    case MYAOS_POSIX_SEEK_END:
        base = (int64_t)handle->size;
        break;
    default:
        return -1;
    }

    next = base + offset;
    if (next < 0 || (uint64_t)next > SCHED_POSIX_FILE_MAX) {
        return -1;
    }
    handle->offset = (uint32_t)next;
    if (out_offset) {
        *out_offset = (uint64_t)handle->offset;
    }
    return 0;
}

int scheduler_posix_fstat(int32_t fd, myaos_posix_stat_t* out) {
    sched_proc_t* proc = current_proc();
    proc_file_handle_t* handle;
    uint32_t mode = SCHED_POSIX_S_IFREG;

    if (!proc || !out) {
        return -1;
    }

    handle = proc_file_handle_for_fd(proc, fd, 0u);
    if (!handle) {
        return -1;
    }

    if (handle->readable) {
        mode |= 0444u;
    }
    if (handle->writable) {
        mode |= 0222u;
    }
    out->size = handle->size;
    out->mode = mode;
    out->uid = proc->uid;
    return 0;
}

int scheduler_posix_dup2(int32_t old_fd, int32_t new_fd) {
    sched_proc_t* proc = current_proc();
    proc_file_handle_t* src;

    if (!proc || old_fd <= 0 || new_fd <= 0 || old_fd > (int32_t)SCHED_MAX_FILE_HANDLES ||
        new_fd > (int32_t)SCHED_MAX_FILE_HANDLES) {
        return -1;
    }
    src = proc_file_handle_for_fd(proc, old_fd, 0u);
    if (!src) {
        return -1;
    }
    if (old_fd == new_fd) {
        return new_fd;
    }

    if (proc->file_handles[(uint32_t)(new_fd - 1)].used) {
        (void)close_proc_file_handle(proc, (int)new_fd);
    }
    if (clone_proc_file_handle(&proc->file_handles[(uint32_t)(new_fd - 1)], src) != 0) {
        return -1;
    }
    return new_fd;
}

int scheduler_posix_poll(myaos_posix_pollfd_t* fds, uint32_t count, uint32_t timeout_ticks, uint32_t* out_ready) {
    sched_proc_t* proc = current_proc();
    uint32_t ready = 0u;

    if (!proc || !out_ready) {
        return -1;
    }
    if (count == 0u) {
        if (timeout_ticks > 0u) {
            scheduler_sleep_current((uint64_t)timeout_ticks);
        }
        *out_ready = 0u;
        return 0;
    }
    if (!fds || count > 256u) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        proc_file_handle_t* handle;
        int16_t revents = 0;

        fds[i].revents = 0;
        handle = proc_file_handle_for_fd(proc, fds[i].fd, 0u);
        if (!handle) {
            fds[i].revents = MYAOS_POSIX_POLLERR;
            ready++;
            continue;
        }

        if ((fds[i].events & MYAOS_POSIX_POLLIN) != 0 && handle->offset < handle->size) {
            revents |= MYAOS_POSIX_POLLIN;
        }
        if ((fds[i].events & MYAOS_POSIX_POLLOUT) != 0) {
            revents |= handle->writable ? MYAOS_POSIX_POLLOUT : MYAOS_POSIX_POLLERR;
        }

        fds[i].revents = revents;
        if (revents != 0) {
            ready++;
        }
    }

    if (ready == 0u && timeout_ticks > 0u) {
        scheduler_sleep_current((uint64_t)timeout_ticks);
    }
    *out_ready = ready;
    return 0;
}

