#include <stdint.h>

extern int dl_host_mul(int a, int b);
extern uint32_t dl_host_magic(void);

int linkdemo_sum(int a, int b) {
    return dl_host_mul(a, b) + 7;
}

uint32_t linkdemo_magic(void) {
    return dl_host_magic() ^ 0x13579BDFu;
}
