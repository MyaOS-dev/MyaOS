#ifndef KERNEL_MODULE_H
#define KERNEL_MODULE_H

#include <myaos/syscall.h>
#include <stdint.h>

#define MODULE_MAX_COUNT 16u

void module_init(void);
void module_register_defaults(void);
int module_register(const char* name, const char* description, uint8_t optional, uint8_t autoload);
int module_set_loaded(const char* name, uint8_t loaded);
int module_is_loaded(const char* name);
uint32_t module_count(void);
uint32_t module_loaded_count(void);
int module_list(myaos_module_info_t* out, uint32_t max_entries, uint32_t* out_count);

#endif
