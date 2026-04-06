#ifndef MYAOS_POSIX_COMPAT_H
#define MYAOS_POSIX_COMPAT_H

#include "myaos.h"

typedef int pid_t;
typedef uint32_t uid_t;
typedef long ssize_t;

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
