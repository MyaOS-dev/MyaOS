#ifndef MYAFS_MODULE_H
#define MYAFS_MODULE_H

#include <stdint.h>

void myafs_module_init(void);
int myafs_module_try_mount(const char* source, const char* mount_path, const char* fs_name);

#endif
