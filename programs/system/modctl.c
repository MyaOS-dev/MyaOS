#include "../lib/myaos.h"

#define MOD_MAX 32u

static void print_usage(void) {
    mya_putln("usage:");
    mya_putln("  modctl");
    mya_putln("  modctl load <name>");
    mya_putln("  modctl unload <name>");
}

static int print_modules(void) {
    myaos_module_info_t mods[MOD_MAX];
    uint32_t count = 0;

    if (mya_mod_list(mods, MOD_MAX, &count) != 0) {
        mya_putln("modctl: list failed");
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        mya_puts(mods[i].name);
        mya_puts(" ");
        mya_puts(mods[i].loaded ? "loaded" : "unloaded");
        mya_puts(" ");
        mya_puts(mods[i].optional ? "optional" : "required");
        if (mods[i].description[0]) {
            mya_puts(" ");
            mya_puts(mods[i].description);
        }
        mya_puts("\n");
    }

    return 0;
}

int program_main(int argc, char** argv) {
    if (argc == 1) {
        return print_modules();
    }

    if (argc == 3 && mya_streq(argv[1], "load")) {
        if (mya_mod_load(argv[2]) != 0) {
            mya_putln("modctl: load failed");
            return 1;
        }
        return print_modules();
    }

    if (argc == 3 && mya_streq(argv[1], "unload")) {
        if (mya_mod_unload(argv[2]) != 0) {
            mya_putln("modctl: unload failed");
            return 1;
        }
        return print_modules();
    }

    print_usage();
    return 1;
}
