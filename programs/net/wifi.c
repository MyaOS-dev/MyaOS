#include "../lib/myaos.h"

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0u; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d == s || n == 0u) {
        return dst;
    }
    if (d < s) {
        for (size_t i = 0u; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0u; i--) {
            d[i - 1u] = s[i - 1u];
        }
    }
    return dst;
}

void* memset(void* dst, int value, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t v = (uint8_t)value;
    for (size_t i = 0u; i < n; i++) {
        d[i] = v;
    }
    return dst;
}

#define WIFI_CFG_PATH "/etc/wifi.conf"
#define WIFI_ETC_PATH "/etc"
#define WIFI_LIB_PATH "/lib"
#define WIFI_FW_DIR "/lib/firmware"
#define WIFI_FW_PATH "/lib/firmware/ax210.ucode"
#define WIFI_FW_MAX_SIZE (4u * 1024u * 1024u)

static void print_help(void) {
    mya_putln("usage:");
    mya_putln("  wifi status");
    mya_putln("  wifi diag");
    mya_putln("  wifi set <ssid> [psk]");
    mya_putln("  wifi show-config");
    mya_putln("  wifi clear-config");
    mya_putln("  wifi firmware-status");
    mya_putln("  wifi firmware-install <source_file>");
    mya_putln("  wifi firmware-remove");
    mya_putln("  wifi connect");
}

static int str_starts_with(const char* text, const char* prefix) {
    uint32_t i = 0u;

    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int str_has_cfg_unsafe_chars(const char* text) {
    if (!text) {
        return 0;
    }
    for (uint32_t i = 0u; text[i]; i++) {
        if (text[i] == '\n' || text[i] == '\r' || text[i] == '=') {
            return 1;
        }
    }
    return 0;
}

static int read_sys_text(const char* key, char* out, uint32_t out_size) {
    char path[MYAOS_PATH_MAX];
    uint8_t buf[192];
    uint32_t size = 0u;
    uint32_t pos = 0u;

    if (!key || !out || out_size == 0u) {
        return -1;
    }

    path[pos++] = '/';
    path[pos++] = 's';
    path[pos++] = 'y';
    path[pos++] = 's';
    path[pos++] = '/';
    for (uint32_t i = 0; key[i] && pos + 1u < sizeof(path); i++) {
        path[pos++] = key[i];
    }
    path[pos] = '\0';

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

static void parse_cfg_value(const char* cfg, const char* key, char* out, uint32_t out_size) {
    uint32_t key_len = 0u;
    uint32_t i = 0u;

    if (!cfg || !key || !out || out_size == 0u) {
        return;
    }

    out[0] = '\0';
    while (key[key_len]) {
        key_len++;
    }

    while (cfg[i]) {
        uint32_t line_start = i;
        uint32_t pos = 0u;

        while (cfg[i] && cfg[i] != '\n') {
            i++;
        }

        if (str_starts_with(&cfg[line_start], key) && cfg[line_start + key_len] == '=') {
            uint32_t j = line_start + key_len + 1u;
            while (cfg[j] && cfg[j] != '\n' && cfg[j] != '\r' && pos + 1u < out_size) {
                out[pos++] = cfg[j++];
            }
            out[pos] = '\0';
            return;
        }

        if (cfg[i] == '\n') {
            i++;
        }
    }
}

static int read_profile(char* out_ssid, uint32_t ssid_size, char* out_psk, uint32_t psk_size) {
    char cfg[320];
    uint32_t size = 0u;

    if (!out_ssid || !out_psk || ssid_size == 0u || psk_size == 0u) {
        return -1;
    }

    out_ssid[0] = '\0';
    out_psk[0] = '\0';

    if (mya_fs_read(WIFI_CFG_PATH, cfg, sizeof(cfg) - 1u, &size) != 0) {
        return -1;
    }
    cfg[size] = '\0';
    parse_cfg_value(cfg, "ssid", out_ssid, ssid_size);
    parse_cfg_value(cfg, "psk", out_psk, psk_size);
    if (!out_ssid[0]) {
        return -1;
    }
    return 0;
}

static void print_status(void) {
    char if_name[MYAOS_NAME_MAX];
    char driver[MYAOS_DEVICE_DRIVER_MAX];
    char model[MYAOS_DESC_MAX];
    char state[32];
    char note[128];
    char fw_state[32];
    char fw_path[128];
    char pci_bdf[32];
    char pci_id[32];

    if (read_sys_text("net_if_name", if_name, sizeof(if_name)) != 0) {
        if_name[0] = '\0';
    }
    if (read_sys_text("net_driver", driver, sizeof(driver)) != 0) {
        driver[0] = '\0';
    }
    if (read_sys_text("net_nic_model", model, sizeof(model)) != 0) {
        model[0] = '\0';
    }
    if (read_sys_text("net_nic_state", state, sizeof(state)) != 0) {
        state[0] = '\0';
    }
    if (read_sys_text("net_nic_note", note, sizeof(note)) != 0) {
        note[0] = '\0';
    }
    if (read_sys_text("net_nic_firmware_state", fw_state, sizeof(fw_state)) != 0) {
        fw_state[0] = '\0';
    }
    if (read_sys_text("net_nic_firmware_path", fw_path, sizeof(fw_path)) != 0) {
        fw_path[0] = '\0';
    }
    if (read_sys_text("net_nic_pci_bdf", pci_bdf, sizeof(pci_bdf)) != 0) {
        pci_bdf[0] = '\0';
    }
    if (read_sys_text("net_nic_pci_id", pci_id, sizeof(pci_id)) != 0) {
        pci_id[0] = '\0';
    }

    mya_puts("if=");
    mya_putln(if_name[0] ? if_name : "none");
    mya_puts("driver=");
    mya_putln(driver[0] ? driver : "none");
    mya_puts("model=");
    mya_putln(model[0] ? model : "none");
    mya_puts("state=");
    mya_putln(state[0] ? state : "none");
    mya_puts("fw_state=");
    mya_putln(fw_state[0] ? fw_state : "n/a");
    mya_puts("fw_path=");
    mya_putln(fw_path[0] ? fw_path : "none");
    mya_puts("pci=");
    mya_puts(pci_bdf[0] ? pci_bdf : "none");
    mya_puts(" ");
    mya_putln(pci_id[0] ? pci_id : "none");
    mya_puts("note=");
    mya_putln(note[0] ? note : "none");

    if (mya_streq(state, "ready")) {
        mya_putln("wifi: link is active");
    } else if (mya_streq(driver, "net.ax210")) {
        if (mya_streq(fw_state, "missing")) {
            mya_putln("wifi: install firmware via `wifi firmware-install <file>`");
        } else {
            mya_putln("wifi: AX210 firmware is visible, kernel 802.11 data path is still in progress");
        }
    }
}

static int save_profile(const char* ssid, const char* psk) {
    char buf[320];
    uint32_t pos = 0u;
    uint32_t psk_len = 0u;

    if (!ssid || !ssid[0]) {
        mya_putln("wifi: empty ssid");
        return 1;
    }
    if (str_has_cfg_unsafe_chars(ssid)) {
        mya_putln("wifi: ssid contains unsupported characters");
        return 1;
    }
    if (psk && str_has_cfg_unsafe_chars(psk)) {
        mya_putln("wifi: psk contains unsupported characters");
        return 1;
    }

    psk_len = (uint32_t)mya_strlen(psk ? psk : "");
    if (psk_len != 0u && (psk_len < 8u || psk_len > 63u)) {
        mya_putln("wifi: psk length must be 8..63 characters");
        return 1;
    }

    (void)mya_fs_mkdir(WIFI_ETC_PATH);

    for (uint32_t i = 0; i < 5u && pos + 1u < sizeof(buf); i++) {
        const char* k = "ssid=";
        buf[pos++] = k[i];
    }
    for (uint32_t i = 0; ssid[i] && pos + 1u < sizeof(buf); i++) {
        buf[pos++] = ssid[i];
    }
    if (pos + 1u < sizeof(buf)) {
        buf[pos++] = '\n';
    }

    for (uint32_t i = 0; i < 4u && pos + 1u < sizeof(buf); i++) {
        const char* k = "psk=";
        buf[pos++] = k[i];
    }
    if (psk) {
        for (uint32_t i = 0; psk[i] && pos + 1u < sizeof(buf); i++) {
            buf[pos++] = psk[i];
        }
    }
    if (pos + 1u < sizeof(buf)) {
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';

    if (mya_fs_write(WIFI_CFG_PATH, buf, pos) != 0) {
        mya_putln("wifi: failed to save profile");
        return 1;
    }

    mya_putln("wifi: profile saved");
    return 0;
}

static int show_profile(void) {
    char ssid[96];
    char psk[96];

    if (read_profile(ssid, sizeof(ssid), psk, sizeof(psk)) != 0) {
        mya_putln("wifi: no profile");
        return 1;
    }

    mya_puts("ssid=");
    mya_putln(ssid);
    mya_puts("psk=");
    if (psk[0]) {
        mya_puts("set(len=");
        mya_put_u32((uint32_t)mya_strlen(psk));
        mya_putln(")");
    } else {
        mya_putln("empty");
    }
    return 0;
}

static int clear_profile(void) {
    if (mya_fs_remove(WIFI_CFG_PATH) != 0) {
        mya_putln("wifi: profile is absent");
        return 1;
    }
    mya_putln("wifi: profile removed");
    return 0;
}

static int print_firmware_status(void) {
    char driver[MYAOS_DEVICE_DRIVER_MAX];
    char state[32];
    char fw_state[32];
    char fw_path[128];
    char note[128];

    if (read_sys_text("net_driver", driver, sizeof(driver)) != 0) {
        driver[0] = '\0';
    }
    if (read_sys_text("net_nic_state", state, sizeof(state)) != 0) {
        state[0] = '\0';
    }
    if (read_sys_text("net_nic_firmware_state", fw_state, sizeof(fw_state)) != 0) {
        fw_state[0] = '\0';
    }
    if (read_sys_text("net_nic_firmware_path", fw_path, sizeof(fw_path)) != 0) {
        fw_path[0] = '\0';
    }
    if (read_sys_text("net_nic_note", note, sizeof(note)) != 0) {
        note[0] = '\0';
    }

    mya_puts("driver=");
    mya_putln(driver[0] ? driver : "none");
    mya_puts("nic_state=");
    mya_putln(state[0] ? state : "none");
    mya_puts("fw_state=");
    mya_putln(fw_state[0] ? fw_state : "n/a");
    mya_puts("fw_path=");
    mya_putln(fw_path[0] ? fw_path : "none");
    mya_puts("note=");
    mya_putln(note[0] ? note : "none");

    if (!mya_streq(driver, "net.ax210")) {
        mya_putln("wifi: AX210 is not the active/probed NIC");
    }
    return 0;
}

static int firmware_install(const char* src_path) {
    uint8_t* buf;
    uint32_t size = 0u;

    if (!src_path || !src_path[0]) {
        mya_putln("wifi: missing firmware source path");
        return 1;
    }

    buf = (uint8_t*)mya_mem_map(WIFI_FW_MAX_SIZE, MYAOS_MEM_MAP_WRITABLE);
    if (!buf) {
        mya_putln("wifi: cannot allocate firmware buffer");
        return 1;
    }

    if (mya_fs_read(src_path, buf, WIFI_FW_MAX_SIZE, &size) != 0 || size == 0u) {
        (void)mya_mem_unmap(buf);
        mya_putln("wifi: failed to read firmware (missing or too large)");
        return 1;
    }

    (void)mya_fs_mkdir(WIFI_LIB_PATH);
    (void)mya_fs_mkdir(WIFI_FW_DIR);
    if (mya_fs_write(WIFI_FW_PATH, buf, size) != 0) {
        (void)mya_mem_unmap(buf);
        mya_putln("wifi: failed to write /lib/firmware/ax210.ucode");
        return 1;
    }

    (void)mya_mem_unmap(buf);
    mya_puts("wifi: firmware installed to ");
    mya_putln(WIFI_FW_PATH);
    mya_puts("wifi: bytes=");
    mya_put_u32(size);
    mya_putln("");
    return 0;
}

static int firmware_remove(void) {
    if (mya_fs_remove(WIFI_FW_PATH) != 0) {
        mya_putln("wifi: firmware file is absent");
        return 1;
    }
    mya_putln("wifi: firmware removed");
    return 0;
}

static int print_diag(void) {
    char value[160];
    const char* keys[] = {
        "net_if_name",
        "net_driver",
        "net_nic_model",
        "net_nic_state",
        "net_nic_note",
        "net_nic_firmware_state",
        "net_nic_firmware_path",
        "net_nic_pci_bdf",
        "net_nic_pci_id",
        "net_nic_detected",
        "net_nic_supported",
    };

    for (uint32_t i = 0u; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (read_sys_text(keys[i], value, sizeof(value)) == 0) {
            mya_puts(keys[i]);
            mya_puts("=");
            mya_putln(value);
        } else {
            mya_puts(keys[i]);
            mya_putln("=<unavailable>");
        }
    }

    return 0;
}

static int connect_now(void) {
    char state[32];
    char note[128];
    char driver[MYAOS_DEVICE_DRIVER_MAX];
    char fw_state[32];
    char fw_path[128];
    char ssid[96];
    char psk[96];

    if (read_sys_text("net_nic_state", state, sizeof(state)) != 0) {
        state[0] = '\0';
    }
    if (read_sys_text("net_nic_note", note, sizeof(note)) != 0) {
        note[0] = '\0';
    }
    if (read_sys_text("net_driver", driver, sizeof(driver)) != 0) {
        driver[0] = '\0';
    }
    if (read_sys_text("net_nic_firmware_state", fw_state, sizeof(fw_state)) != 0) {
        fw_state[0] = '\0';
    }
    if (read_sys_text("net_nic_firmware_path", fw_path, sizeof(fw_path)) != 0) {
        fw_path[0] = '\0';
    }

    if (mya_streq(state, "ready")) {
        mya_putln("wifi: already connected");
        return 0;
    }

    if (read_profile(ssid, sizeof(ssid), psk, sizeof(psk)) != 0) {
        mya_putln("wifi: profile is not configured");
        mya_putln("wifi: set profile via `wifi set <ssid> [psk]`");
        return 1;
    }

    mya_puts("wifi: profile ssid=");
    mya_putln(ssid);

    if (mya_streq(driver, "net.ax210")) {
        if (mya_streq(fw_state, "missing")) {
            mya_putln("wifi: AX210 firmware is missing");
            mya_putln("wifi: run `wifi firmware-install <path-to-ucode>`");
            return 2;
        }

        mya_putln("wifi: AX210 firmware detected");
        if (fw_path[0] && !mya_streq(fw_path, "none")) {
            mya_puts("wifi: firmware path=");
            mya_putln(fw_path);
        }
        mya_putln("wifi: association/auth/TX/RX for AX210 is not implemented yet");
        if (note[0]) {
            mya_puts("wifi: ");
            mya_putln(note);
        }
        return 2;
    }

    mya_putln("wifi: no active Wi-Fi adapter");
    return 1;
}

int program_main(int argc, char** argv) {
    if (argc <= 1 || mya_streq(argv[1], "status")) {
        print_status();
        return 0;
    }

    if (mya_streq(argv[1], "diag")) {
        return print_diag();
    }

    if (mya_streq(argv[1], "set")) {
        if (argc < 3) {
            mya_putln("wifi: missing ssid");
            print_help();
            return 1;
        }
        return save_profile(argv[2], argc >= 4 ? argv[3] : "");
    }

    if (mya_streq(argv[1], "show-config")) {
        return show_profile();
    }

    if (mya_streq(argv[1], "clear-config")) {
        return clear_profile();
    }

    if (mya_streq(argv[1], "firmware-status")) {
        return print_firmware_status();
    }

    if (mya_streq(argv[1], "firmware-install")) {
        if (argc < 3) {
            mya_putln("wifi: missing source firmware path");
            print_help();
            return 1;
        }
        return firmware_install(argv[2]);
    }

    if (mya_streq(argv[1], "firmware-remove")) {
        return firmware_remove();
    }

    if (mya_streq(argv[1], "connect")) {
        return connect_now();
    }

    print_help();
    return 1;
}
