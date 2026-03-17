#include "libc.h"

#define MSG_MAX 128

int program_main(int argc, char** argv) {
    uint64_t pid = 0;
    char msg[MSG_MAX];
    size_t pos = 0;

    if (argc < 3) {
        mya_putln("usage: msgsend <pid> <message>");
        return 1;
    }

    if (mya_strto_u64(argv[1], &pid) != 0) {
        mya_putln("msgsend: invalid pid");
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        const char* part = argv[i];
        for (size_t j = 0; part[j] && pos + 1 < sizeof(msg); j++) {
            msg[pos++] = part[j];
        }
        if (i + 1 < argc && pos + 1 < sizeof(msg)) {
            msg[pos++] = ' ';
        }
    }
    msg[pos] = '\0';

    if (mya_proc_msg_send((int32_t)pid, msg) != 0) {
        mya_putln("msgsend: failed");
        return 1;
    }

    return 0;
}
