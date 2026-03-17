#include "module.h"
#include <stddef.h>

typedef struct {
    uint8_t used;
    uint8_t loaded;
    uint8_t optional;
    uint8_t reserved0;
    char name[MYAOS_NAME_MAX];
    char description[MYAOS_DESC_MAX];
} module_entry_t;

static module_entry_t g_modules[MODULE_MAX_COUNT];
static uint32_t g_module_count;

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0;

    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1u < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_eq(const char* a, const char* b) {
    uint32_t i = 0;

    if (!a || !b) {
        return 0;
    }

    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }

    return a[i] == b[i];
}

static int is_valid_name(const char* name) {
    uint32_t n = 0;

    if (!name || !name[0]) {
        return 0;
    }

    for (n = 0; name[n]; n++) {
        char c = name[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return 0;
        }
    }

    return n < MYAOS_NAME_MAX;
}

static int find_module_slot(const char* name) {
    if (!name || !name[0]) {
        return -1;
    }

    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        if (!g_modules[i].used) {
            continue;
        }
        if (str_eq(g_modules[i].name, name)) {
            return (int)i;
        }
    }

    return -1;
}

void module_init(void) {
    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        g_modules[i].used = 0u;
        g_modules[i].loaded = 0u;
        g_modules[i].optional = 0u;
        g_modules[i].reserved0 = 0u;
        g_modules[i].name[0] = '\0';
        g_modules[i].description[0] = '\0';
    }
    g_module_count = 0u;
}

int module_register(const char* name, const char* description, uint8_t optional, uint8_t autoload) {
    int slot;

    if (!is_valid_name(name)) {
        return -1;
    }

    slot = find_module_slot(name);
    if (slot < 0) {
        if (g_module_count >= MODULE_MAX_COUNT) {
            return -1;
        }

        for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
            if (!g_modules[i].used) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) {
            return -1;
        }

        g_modules[slot].used = 1u;
        g_module_count++;
    }

    str_copy(g_modules[slot].name, name, sizeof(g_modules[slot].name));
    str_copy(g_modules[slot].description, description ? description : "", sizeof(g_modules[slot].description));
    g_modules[slot].optional = optional ? 1u : 0u;
    g_modules[slot].loaded = autoload ? 1u : 0u;
    return 0;
}

void module_register_defaults(void) {
    (void)module_register("core", "core kernel services", 0u, 1u);
    (void)module_register("net", "network stack and sockets", 0u, 1u);
    (void)module_register("swap", "explicit swap subsystem", 1u, 1u);
    (void)module_register("hotplug", "runtime ramdisk hot-plug", 1u, 1u);
    (void)module_register("ldso", "user-space dynamic loader support", 1u, 1u);
    (void)module_register("posix", "POSIX compatibility layer", 1u, 1u);
}

int module_set_loaded(const char* name, uint8_t loaded) {
    int slot = find_module_slot(name);

    if (slot < 0) {
        return -1;
    }
    if (!loaded && !g_modules[slot].optional) {
        return -2;
    }

    g_modules[slot].loaded = loaded ? 1u : 0u;
    return 0;
}

int module_is_loaded(const char* name) {
    int slot = find_module_slot(name);

    if (slot < 0) {
        return 0;
    }

    return g_modules[slot].loaded ? 1 : 0;
}

uint32_t module_count(void) {
    return g_module_count;
}

uint32_t module_loaded_count(void) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        if (g_modules[i].used && g_modules[i].loaded) {
            count++;
        }
    }

    return count;
}

int module_list(myaos_module_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t count = 0;

    if (!out_count) {
        return -1;
    }

    for (uint32_t i = 0; i < MODULE_MAX_COUNT; i++) {
        if (!g_modules[i].used) {
            continue;
        }

        if (count < max_entries && out) {
            str_copy(out[count].name, g_modules[i].name, sizeof(out[count].name));
            str_copy(out[count].description, g_modules[i].description, sizeof(out[count].description));
            out[count].loaded = g_modules[i].loaded;
            out[count].optional = g_modules[i].optional;
            out[count].reserved0 = 0u;
        }
        count++;
    }

    if (count > max_entries) {
        count = max_entries;
    }
    *out_count = count;
    return 0;
}
