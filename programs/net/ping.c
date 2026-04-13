#include "../lib/myaos.h"
#include "../lib/netutil.h"

int program_main(int argc, char** argv) {
    uint32_t dst_ip = 0u;
    uint32_t count = 4u;
    uint32_t timeout_polls = 12000u;
    uint32_t ok = 0u;
    uint32_t lost = 0u;
    uint16_t ident = (uint16_t)(mya_proc_getpid() & 0xFFFFu);

    if (argc < 2) {
        mya_putln("usage: ping <host_or_ipv4> [count] [timeout_polls]");
        return 1;
    }

    if (argc > 2) {
        uint64_t parsed = 0u;
        if (mya_strto_u64(argv[2], &parsed) == 0 && parsed > 0u && parsed <= 1000u) {
            count = (uint32_t)parsed;
        }
    }
    if (argc > 3) {
        uint64_t parsed = 0u;
        if (mya_strto_u64(argv[3], &parsed) == 0 && parsed > 0u && parsed <= 0xFFFFFFFFu) {
            timeout_polls = (uint32_t)parsed;
        }
    }
    if (mya_dns_resolve_ipv4(argv[1], timeout_polls, &dst_ip) != 0) {
        mya_puts("ping: cannot resolve target: ");
        mya_putln(argv[1]);
        return 1;
    }

    mya_puts("PING ");
    mya_puts(argv[1]);
    mya_puts(" (");
    mya_net_print_ipv4(dst_ip);
    mya_puts(")");
    mya_puts(" count=");
    mya_put_u32(count);
    mya_puts("\n");

    for (uint32_t i = 0u; i < count; i++) {
        uint16_t seq = (uint16_t)(i + 1u);
        uint32_t rtt_ticks = 0u;

        if (mya_net_ping4(dst_ip, ident, seq, timeout_polls, &rtt_ticks) == 0) {
            ok++;
            mya_puts("reply from ");
            mya_net_print_ipv4(dst_ip);
            mya_puts(": icmp_seq=");
            mya_put_u32((uint32_t)seq);
            mya_puts(" rtt_ticks=");
            mya_put_u32(rtt_ticks);
            mya_puts("\n");
        } else {
            lost++;
            mya_puts("timeout from ");
            mya_net_print_ipv4(dst_ip);
            mya_puts(": icmp_seq=");
            mya_put_u32((uint32_t)seq);
            mya_puts("\n");
        }

        if (i + 1u < count) {
            mya_proc_sleep(25u);
        }
    }

    mya_puts("summary: sent=");
    mya_put_u32(count);
    mya_puts(" received=");
    mya_put_u32(ok);
    mya_puts(" lost=");
    mya_put_u32(lost);
    mya_puts("\n");
    return (ok > 0u) ? 0 : 2;
}
