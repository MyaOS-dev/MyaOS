#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

void interrupts_init(void);
void interrupts_enable(void);
void interrupts_disable(void);
uint64_t interrupts_timer_ticks(void);

#endif
