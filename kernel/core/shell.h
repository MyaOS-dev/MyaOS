#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

void shell_init(void);
void shell_set_recovery_mode(uint8_t enabled);
int shell_main(int argc, char** argv);
int shell_idle_main(int argc, char** argv);

#endif
