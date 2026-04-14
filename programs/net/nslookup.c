#include "../lib/myaos.h"
#include "../lib/netutil.h"

int program_main(int argc, char** argv) {
    uint32_t timeout_polls = MYA_DNS_TIMEOUT_POLLS_DEFAULT;
    uint32_t dns_ip = 0u;
    uint32_t out_ip = 0u;

    if (argc < 2 || !argv[1]) {
        mya_putln("usage: nslookup <host_or_ipv4> [timeout_polls]");
        return 1;
    }

    if (argc > 2) {
        uint64_t parsed = 0u;
        if (mya_strto_u64(argv[2], &parsed) != 0 || parsed == 0u || parsed > 0xFFFFFFFFull) {
            mya_putln("nslookup: invalid timeout_polls");
            return 1;
        }
        timeout_polls = (uint32_t)parsed;
    }

    if (mya_net_default_dns_ip(&dns_ip) == 0 && dns_ip != 0u) {
        mya_puts("server: ");
        mya_net_print_ipv4(dns_ip);
        mya_puts("\n");
    } else {
        mya_putln("server: (unavailable)");
    }

    if (mya_dns_resolve_ipv4(argv[1], timeout_polls, &out_ip) != 0) {
        mya_puts("nslookup: failed to resolve ");
        mya_putln(argv[1]);
        return 2;
    }

    mya_puts("name: ");
    mya_putln(argv[1]);
    mya_puts("address: ");
    mya_net_print_ipv4(out_ip);
    mya_puts("\n");
    return 0;
}
