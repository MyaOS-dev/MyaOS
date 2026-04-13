#ifndef MYAOS_POSIX_COMPAT_H
#define MYAOS_POSIX_COMPAT_H

#include "myaos.h"

typedef int pid_t;
typedef uint32_t uid_t;
typedef long ssize_t;
typedef long off_t;

struct stat {
    uint64_t st_size;
    uint32_t st_mode;
    uint32_t st_uid;
};

struct pollfd {
    int fd;
    short events;
    short revents;
};

enum {
    O_RDONLY = MYAOS_POSIX_O_RDONLY,
    O_WRONLY = MYAOS_POSIX_O_WRONLY,
    O_RDWR = MYAOS_POSIX_O_RDWR,
    O_CREAT = MYAOS_POSIX_O_CREAT,
    O_TRUNC = MYAOS_POSIX_O_TRUNC,
    O_APPEND = MYAOS_POSIX_O_APPEND,
};

enum {
    SEEK_SET = MYAOS_POSIX_SEEK_SET,
    SEEK_CUR = MYAOS_POSIX_SEEK_CUR,
    SEEK_END = MYAOS_POSIX_SEEK_END,
};

enum {
    POLLIN = MYAOS_POSIX_POLLIN,
    POLLOUT = MYAOS_POSIX_POLLOUT,
    POLLERR = MYAOS_POSIX_POLLERR,
};

static inline pid_t getpid(void) {
    return (pid_t)mya_proc_getpid();
}

static inline uid_t getuid(void) {
    return (uid_t)mya_sec_whoami();
}

static inline int chdir(const char* path) {
    return mya_fs_chdir(path);
}

static inline char* getcwd(char* out, uint32_t out_size) {
    if (!out || out_size == 0u) {
        return NULL;
    }
    return mya_fs_getcwd(out, out_size) == 0 ? out : NULL;
}

static inline int kill(pid_t pid, int signal_number) {
    int32_t exit_code = (signal_number >= 0) ? (128 + signal_number) : 1;
    return mya_proc_kill(pid, exit_code);
}

static inline int open(const char* path, int flags, ...) {
    int32_t fd = -1;
    if (mya_posix_open(path, (uint32_t)flags, &fd) != 0) {
        return -1;
    }
    return (int)fd;
}

static inline int close(int fd) {
    return mya_posix_close((int32_t)fd);
}

static inline ssize_t read(int fd, void* buf, size_t count) {
    uint32_t n = 0u;
    if (mya_posix_read((int32_t)fd, buf, (uint32_t)count, &n) != 0) {
        return -1;
    }
    return (ssize_t)n;
}

static inline ssize_t write(int fd, const void* buf, size_t count) {
    uint32_t n = 0u;
    if (mya_posix_write((int32_t)fd, buf, (uint32_t)count, &n) != 0) {
        return -1;
    }
    return (ssize_t)n;
}

static inline off_t lseek(int fd, off_t offset, int whence) {
    uint64_t next = 0u;
    if (mya_posix_lseek((int32_t)fd, (int64_t)offset, (uint32_t)whence, &next) != 0) {
        return (off_t)-1;
    }
    return (off_t)next;
}

static inline int fstat(int fd, struct stat* out) {
    myaos_posix_stat_t st;
    if (!out) {
        return -1;
    }
    if (mya_posix_fstat((int32_t)fd, &st) != 0) {
        return -1;
    }
    out->st_size = st.size;
    out->st_mode = st.mode;
    out->st_uid = st.uid;
    return 0;
}

static inline int dup2(int oldfd, int newfd) {
    return mya_posix_dup2((int32_t)oldfd, (int32_t)newfd);
}

static inline int poll(struct pollfd* fds, uint32_t nfds, int timeout_ticks) {
    myaos_posix_pollfd_t tmp[64];
    myaos_posix_pollfd_t* in = NULL;
    uint32_t ready = 0u;
    uint32_t timeout = timeout_ticks > 0 ? (uint32_t)timeout_ticks : 0u;

    if ((!fds && nfds != 0u) || nfds > (uint32_t)(sizeof(tmp) / sizeof(tmp[0]))) {
        return -1;
    }

    if (nfds == 0u) {
        return mya_posix_poll(NULL, 0u, timeout, &ready);
    }

    for (uint32_t i = 0; i < nfds; i++) {
        tmp[i].fd = fds[i].fd;
        tmp[i].events = (int16_t)fds[i].events;
        tmp[i].revents = 0;
    }
    in = tmp;

    if (mya_posix_poll(in, nfds, timeout, &ready) != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < nfds; i++) {
        fds[i].revents = (short)in[i].revents;
    }
    return (int)ready;
}

static inline unsigned int sleep(unsigned int seconds) {
    mya_proc_sleep((uint64_t)seconds * 100u);
    return 0u;
}

static inline int usleep(unsigned int usec) {
    uint64_t ticks = (uint64_t)(usec + 9999u) / 10000u;
    if (ticks == 0u) {
        ticks = 1u;
    }
    mya_proc_sleep(ticks);
    return 0;
}

#endif
