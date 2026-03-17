#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    myaos_netinfo_t info;
    myaos_device_info_t devs[32];
    uint32_t dev_count = 0;
    uint32_t net_dev_count = 0;

    (void)argc;
    (void)argv;

    if (mya_net_info(&info) != 0) {
        mya_putln("netstat: failed to read net info");
        return 1;
    }

    if (mya_dev_list(devs, 32u, &dev_count) == 0) {
        for (uint32_t i = 0; i < dev_count; i++) {
            if (devs[i].class_id == MYAOS_DEV_NETWORK) {
                net_dev_count++;
            }
        }
    }

    mya_puts("net_devices=");
    mya_put_u32(net_dev_count);
    mya_puts("\n");
    mya_puts("sockets=");
    mya_put_u32(info.socket_count);
    mya_puts("\n");
    mya_puts("tx_packets=");
    mya_put_u64(info.tx_packets);
    mya_puts("\n");
    mya_puts("rx_packets=");
    mya_put_u64(info.rx_packets);
    mya_puts("\n");
    mya_puts("dropped_packets=");
    mya_put_u64(info.dropped_packets);
    mya_puts("\n");

    return 0;
}
