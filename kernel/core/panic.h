#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

void panic_message(const char* message);
void panic_exception(uint64_t vector, uint64_t error_code, uint64_t rip);

#endif
