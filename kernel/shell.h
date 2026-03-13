#ifndef SHELL_H
#define SHELL_H

#include "boot.h"
#include <stdint.h>

enum {
    SHELL_API_CONSOLE = 0x1000,
    SHELL_API_SYSTEM = 0x2000,
    SHELL_API_POWER = 0x3000,
    SHELL_API_FAT = 0x4000,
    SHELL_API_RAM = 0x5000,
    SHELL_API_STATS = 0x6000,
};

enum {
    SHELL_OP_CONSOLE_CLEAR = 1,
    SHELL_OP_SYSTEM_ABOUT = 1,
    SHELL_OP_SYSTEM_ECHO = 2,
    SHELL_OP_POWER_HALT = 1,
    SHELL_OP_POWER_REBOOT = 2,
    SHELL_OP_POWER_SHUTDOWN = 3,
    SHELL_OP_FAT_INFO = 1,
    SHELL_OP_FAT_PWD = 2,
    SHELL_OP_FAT_LS = 3,
    SHELL_OP_FAT_CD = 4,
    SHELL_OP_FAT_CAT = 5,
    SHELL_OP_FAT_MKDIR = 6,
    SHELL_OP_FAT_TOUCH = 7,
    SHELL_OP_FAT_WRITE = 8,
    SHELL_OP_FAT_FLUSH = 9,
    SHELL_OP_RAM_LS = 1,
    SHELL_OP_RAM_WRITE = 2,
    SHELL_OP_RAM_CAT = 3,
    SHELL_OP_RAM_RM = 4,
    SHELL_OP_RAM_CLEAR = 5,
    SHELL_OP_STATS_MEMINFO = 1,
    SHELL_OP_STATS_SCHEDINFO = 2,
};

void shell_init(boot_info_t* boot);
void shell_step(void);
void shell_start(boot_info_t* boot);
int shell_syscall_run_api(uint32_t api_addr, uint32_t op_code, const char* args);

#endif
