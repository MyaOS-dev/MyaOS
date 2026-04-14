#include "../lib/myaos.h"

static int parse_u32(const char* text, uint32_t* out) {
    uint64_t value = 0u;

    if (!text || !text[0] || !out) {
        return -1;
    }

    for (uint32_t i = 0; text[i]; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
        if (value > 0xFFFFFFFFull) {
            return -1;
        }
    }

    *out = (uint32_t)value;
    return 0;
}

static int read_sys_text(const char* key, char* out, uint32_t out_size) {
    char path[MYAOS_PATH_MAX];
    uint8_t buf[128];
    uint32_t path_pos = 0u;
    uint32_t size = 0u;

    if (!key || !out || out_size == 0u) {
        return -1;
    }

    path[path_pos++] = '/';
    path[path_pos++] = 's';
    path[path_pos++] = 'y';
    path[path_pos++] = 's';
    path[path_pos++] = '/';
    for (uint32_t i = 0; key[i] && path_pos + 1u < sizeof(path); i++) {
        path[path_pos++] = key[i];
    }
    path[path_pos] = '\0';

    if (mya_fs_read(path, buf, sizeof(buf) - 1u, &size) != 0) {
        return -1;
    }
    if (size >= out_size) {
        size = out_size - 1u;
    }
    for (uint32_t i = 0; i < size; i++) {
        out[i] = (char)buf[i];
    }
    out[size] = '\0';
    return 0;
}

static int read_sys_u32(const char* key, uint32_t* out) {
    char text[32];
    if (read_sys_text(key, text, sizeof(text)) != 0) {
        return -1;
    }
    return parse_u32(text, out);
}

int program_main(int argc, char** argv) {
    myaos_netinfo_t info;
    myaos_device_info_t devs[32];
    uint32_t dev_count = 0;
    uint32_t net_dev_count = 0;
    uint32_t nic_detected = 0u;
    uint32_t nic_supported = 0u;
    char if_name[MYAOS_NAME_MAX];
    char driver[MYAOS_DEVICE_DRIVER_MAX];
    char model[MYAOS_DESC_MAX];
    char nic_state[32];
    char nic_note[128];
    char nic_fw_state[32];
    char nic_fw_path[128];
    char nic_pci_bdf[32];
    char nic_pci_id[32];

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
    if (read_sys_text("net_if_name", if_name, sizeof(if_name)) == 0) {
        mya_puts("if_name=");
        mya_puts(if_name);
        mya_puts("\n");
    }
    if (read_sys_text("net_driver", driver, sizeof(driver)) == 0) {
        mya_puts("driver=");
        mya_puts(driver);
        mya_puts("\n");
    }
    if (read_sys_text("net_nic_model", model, sizeof(model)) == 0) {
        mya_puts("nic_model=");
        mya_puts(model);
        mya_puts("\n");
    }
    if (read_sys_text("net_nic_state", nic_state, sizeof(nic_state)) == 0) {
        mya_puts("nic_state=");
        mya_puts(nic_state);
        mya_puts("\n");
    }
    if (read_sys_text("net_nic_note", nic_note, sizeof(nic_note)) == 0) {
        mya_puts("nic_note=");
        mya_puts(nic_note);
        mya_puts("\n");
    }
    if (read_sys_text("net_nic_firmware_state", nic_fw_state, sizeof(nic_fw_state)) == 0) {
        mya_puts("nic_fw_state=");
        mya_puts(nic_fw_state);
        mya_puts("\n");
    }
    if (read_sys_text("net_nic_firmware_path", nic_fw_path, sizeof(nic_fw_path)) == 0) {
        mya_puts("nic_fw_path=");
        mya_puts(nic_fw_path);
        mya_puts("\n");
    }
    if (read_sys_text("net_nic_pci_bdf", nic_pci_bdf, sizeof(nic_pci_bdf)) == 0) {
        mya_puts("nic_pci_bdf=");
        mya_puts(nic_pci_bdf);
        mya_puts("\n");
    }
    if (read_sys_text("net_nic_pci_id", nic_pci_id, sizeof(nic_pci_id)) == 0) {
        mya_puts("nic_pci_id=");
        mya_puts(nic_pci_id);
        mya_puts("\n");
    }
    if (read_sys_u32("net_nic_detected", &nic_detected) == 0) {
        mya_puts("nic_detected=");
        mya_put_u32(nic_detected);
        mya_puts("\n");
    }
    if (read_sys_u32("net_nic_supported", &nic_supported) == 0) {
        mya_puts("nic_supported=");
        mya_put_u32(nic_supported);
        mya_puts("\n");
    }
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
