#ifndef SYSCALL_H
#define SYSCALL_H

#include "boot.h"
#include <stdint.h>

void syscall_set_boot_info(boot_info_t* boot);
int64_t syscall_dispatch(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
);
int64_t syscall_dispatch_linux(
    uint64_t number,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5,
    uint64_t frame_rsp
);

#endif
