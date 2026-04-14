#ifndef POWER_H
#define POWER_H

#include "boot.h"

void power_init(boot_info_t* boot);
void power_shutdown(boot_info_t* boot);
void power_reboot(boot_info_t* boot);
int power_set_gop_mode_pref(boot_info_t* boot, uint8_t has_mode, uint32_t mode);
int power_get_gop_mode_pref(boot_info_t* boot, uint8_t* out_has_mode, uint32_t* out_mode);

#endif
