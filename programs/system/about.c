#include "../lib/myaos.h"

int program_main(int argc, char** argv) {
    const char* mode = (argc > 1 && argv[1]) ? argv[1] : "all";

    if (mya_streq(mode, "all") || mya_streq(mode, "what")) {
        mya_putln("what: MyaOS is an experimental monolithic kernel OS");
        mya_putln("what: includes VFS, ELF loader, processes, syscalls, and devices");
        mya_putln("what: platform is not Unix-based; Unix-like commands are legacy compatibility aliases");
    }

    if (mya_streq(mode, "all") || mya_streq(mode, "where")) {
        mya_putln("where: boot script /autorun.sh (or /boot/autorun.sh)");
        mya_putln("where: command manifests in /cmd (or /boot/cmd)");
        mya_putln("where: programs in /bin (or /boot/bin)");
    }

    if (mya_streq(mode, "all") || mya_streq(mode, "debug")) {
        mya_putln("debug: run `debug on` in shell for command trace");
        mya_putln("debug: inspect /sys/init_failed_* and /var/log");
        mya_putln("debug: use checks: seccheck, schedcheck, p0check");
    }

    if (!mya_streq(mode, "all") && !mya_streq(mode, "what") &&
        !mya_streq(mode, "where") && !mya_streq(mode, "debug")) {
        mya_putln("usage: about [what|where|debug]");
        return 1;
    }
    return 0;
}
