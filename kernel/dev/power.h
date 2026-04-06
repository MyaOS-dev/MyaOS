#ifndef POWER_H
#define POWER_H

#include "boot.h"

void power_init(boot_info_t* boot);
void power_shutdown(boot_info_t* boot);
void power_reboot(boot_info_t* boot);

#endif
