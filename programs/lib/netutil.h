#ifndef MYAOS_NETUTIL_H
#define MYAOS_NETUTIL_H

#include "myaos.h"

#define MYA_DNS_PACKET_MAX 512u
#define MYA_DNS_PORT 53u
#define MYA_DNS_TIMEOUT_POLLS_DEFAULT 30000u
#define MYA_DNS_SRC_PORT_MIN 49152u
#define MYA_DNS_SRC_PORT_MAX 65535u
#define MYA_DNS_PUBLIC1 0x08080808u /* 8.8.8.8 */
#define MYA_DNS_PUBLIC2 0x01010101u /* 1.1.1.1 */
#define MYA_DNS_RETRIES_PER_SERVER 3u

static inline uint16_t mya_net_read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t mya_net_read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static inline void mya_net_write_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static inline int mya_net_parse_u32_text(const char* text, uint32_t* out) {
    uint64_t value = 0u;

    if (!text || !text[0] || !out || mya_strto_u64(text, &value) != 0 || value > 0xFFFFFFFFull) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static inline int mya_net_read_sys_u32(const char* key, uint32_t* out) {
    char path[MYAOS_PATH_MAX];
    uint8_t buf[64];
    uint32_t size = 0u;
    uint32_t pos = 0u;

    if (!key || !out) {
        return -1;
    }

    path[pos++] = '/';
    path[pos++] = 's';
    path[pos++] = 'y';
    path[pos++] = 's';
    path[pos++] = '/';
    for (uint32_t i = 0u; key[i] && pos + 1u < sizeof(path); i++) {
        path[pos++] = key[i];
    }
    path[pos] = '\0';

    if (mya_fs_read(path, buf, sizeof(buf) - 1u, &size) != 0) {
        return -1;
    }
    buf[size] = '\0';
    return mya_net_parse_u32_text((const char*)buf, out);
}

static inline int mya_net_parse_ipv4(const char* text, uint32_t* out_ip) {
    uint32_t part = 0u;
    uint32_t value = 0u;
    uint32_t p0 = 0u;
    uint32_t p1 = 0u;
    uint32_t p2 = 0u;
    uint32_t p3 = 0u;
    uint8_t had_digit = 0u;

    if (!text || !out_ip) {
        return -1;
    }

    for (uint32_t i = 0u;; i++) {
        char c = text[i];

        if (c >= '0' && c <= '9') {
            value = value * 10u + (uint32_t)(c - '0');
            if (value > 255u) {
                return -1;
            }
            had_digit = 1u;
            continue;
        }

        if (c == '.' || c == '\0') {
            if (!had_digit || part >= 4u) {
                return -1;
            }
            if (part == 0u) {
                p0 = value;
            } else if (part == 1u) {
                p1 = value;
            } else if (part == 2u) {
                p2 = value;
            } else {
                p3 = value;
            }
            part++;
            value = 0u;
            had_digit = 0u;
            if (c == '\0') {
                break;
            }
            continue;
        }

        return -1;
    }

    if (part != 4u) {
        return -1;
    }

    *out_ip = (p0 << 24) | (p1 << 16) | (p2 << 8) | p3;
    return 0;
}

static inline void mya_net_print_ipv4(uint32_t ip) {
    mya_put_u32((ip >> 24) & 0xFFu);
    mya_puts(".");
    mya_put_u32((ip >> 16) & 0xFFu);
    mya_puts(".");
    mya_put_u32((ip >> 8) & 0xFFu);
    mya_puts(".");
    mya_put_u32(ip & 0xFFu);
}

static inline int mya_net_default_dns_ip(uint32_t* out_dns_ip) {
    uint32_t dns_ip = 0u;

    if (!out_dns_ip) {
        return -1;
    }
    if (mya_net_read_sys_u32("net_dns_ip", &dns_ip) != 0 || dns_ip == 0u) {
        if (mya_net_read_sys_u32("net_gateway_ip", &dns_ip) != 0 || dns_ip == 0u) {
            return -1;
        }
    }
    *out_dns_ip = dns_ip;
    return 0;
}

static inline int mya_dns_skip_name(const uint8_t* msg, uint32_t size, uint32_t* io_off) {
    uint32_t walk;
    uint8_t jumped = 0u;

    if (!msg || !io_off || *io_off >= size) {
        return -1;
    }

    walk = *io_off;
    for (uint32_t depth = 0u; depth < 64u; depth++) {
        uint8_t len;

        if (walk >= size) {
            return -1;
        }
        len = msg[walk];
        if (len == 0u) {
            if (!jumped) {
                *io_off = walk + 1u;
            }
            return 0;
        }

        if ((len & 0xC0u) == 0xC0u) {
            uint16_t ptr;
            if (walk + 1u >= size) {
                return -1;
            }
            ptr = (uint16_t)(((uint16_t)(len & 0x3Fu) << 8) | msg[walk + 1u]);
            if (ptr >= size) {
                return -1;
            }
            if (!jumped) {
                *io_off = walk + 2u;
                jumped = 1u;
            }
            walk = (uint32_t)ptr;
            continue;
        }

        if ((len & 0xC0u) != 0u || len > 63u) {
            return -1;
        }

        walk++;
        if (walk + (uint32_t)len > size) {
            return -1;
        }
        walk += (uint32_t)len;
        if (!jumped) {
            *io_off = walk;
        }
    }

    return -1;
}

static inline int mya_dns_build_query(const char* host, uint16_t txid, uint8_t* out_packet, uint32_t* out_len) {
    uint32_t host_len = 0u;
    uint32_t pos = 0u;
    uint32_t label_start = 0u;
    uint32_t qname_start;

    if (!host || !out_packet || !out_len) {
        return -1;
    }

    while (host[host_len]) {
        host_len++;
    }
    if (host_len == 0u) {
        return -1;
    }
    if (host[host_len - 1u] == '.') {
        host_len--;
    }
    if (host_len == 0u) {
        return -1;
    }

    if (MYA_DNS_PACKET_MAX < 12u) {
        return -1;
    }

    for (uint32_t i = 0u; i < MYA_DNS_PACKET_MAX; i++) {
        out_packet[i] = 0u;
    }

    mya_net_write_be16(out_packet + 0u, txid);
    mya_net_write_be16(out_packet + 2u, 0x0100u);
    mya_net_write_be16(out_packet + 4u, 1u);

    pos = 12u;
    qname_start = pos;

    for (uint32_t i = 0u; i <= host_len; i++) {
        char c = (i == host_len) ? '.' : host[i];
        if (c == '.') {
            uint32_t label_len = i - label_start;
            if (label_len == 0u || label_len > 63u || pos + 1u + label_len >= MYA_DNS_PACKET_MAX) {
                return -1;
            }
            out_packet[pos++] = (uint8_t)label_len;
            for (uint32_t j = 0u; j < label_len; j++) {
                out_packet[pos++] = (uint8_t)host[label_start + j];
            }
            label_start = i + 1u;
            continue;
        }

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '_')) {
            return -1;
        }
    }

    if (pos >= MYA_DNS_PACKET_MAX) {
        return -1;
    }
    out_packet[pos++] = 0u;

    if (pos + 4u > MYA_DNS_PACKET_MAX || pos <= qname_start + 1u) {
        return -1;
    }
    mya_net_write_be16(out_packet + pos, 1u);
    pos += 2u;
    mya_net_write_be16(out_packet + pos, 1u);
    pos += 2u;

    *out_len = pos;
    return 0;
}

static inline int mya_dns_parse_response(
    const uint8_t* packet,
    uint32_t packet_len,
    uint16_t txid,
    uint32_t* out_ip
) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint32_t off = 12u;

    if (!packet || !out_ip || packet_len < 12u) {
        return -1;
    }

    id = mya_net_read_be16(packet + 0u);
    flags = mya_net_read_be16(packet + 2u);
    qdcount = mya_net_read_be16(packet + 4u);
    ancount = mya_net_read_be16(packet + 6u);

    if (id != txid) {
        return -1;
    }
    if ((flags & 0x8000u) == 0u) {
        return -1;
    }
    if ((flags & 0x000Fu) != 0u) {
        return -1;
    }

    for (uint16_t i = 0u; i < qdcount; i++) {
        if (mya_dns_skip_name(packet, packet_len, &off) != 0 || off + 4u > packet_len) {
            return -1;
        }
        off += 4u;
    }

    for (uint16_t i = 0u; i < ancount; i++) {
        uint16_t type;
        uint16_t klass;
        uint16_t rdlen;

        if (mya_dns_skip_name(packet, packet_len, &off) != 0 || off + 10u > packet_len) {
            return -1;
        }
        type = mya_net_read_be16(packet + off + 0u);
        klass = mya_net_read_be16(packet + off + 2u);
        rdlen = mya_net_read_be16(packet + off + 8u);
        off += 10u;
        if (off + rdlen > packet_len) {
            return -1;
        }

        if (type == 1u && klass == 1u && rdlen == 4u) {
            *out_ip = mya_net_read_be32(packet + off);
            return 0;
        }
        off += rdlen;
    }

    return -1;
}

static inline int mya_dns_resolve_ipv4(const char* host, uint32_t timeout_polls, uint32_t* out_ip) {
    uint32_t dns_ip = 0u;
    uint32_t gateway_ip = 0u;
    uint32_t servers[4];
    uint32_t server_count = 0u;
    uint32_t timeout_per_try = 0u;
    uint32_t budget_left = 0u;
    uint32_t pid;
    uint16_t txid;
    uint8_t packet[MYA_DNS_PACKET_MAX];
    uint32_t query_len = 0u;
    uint32_t span = (MYA_DNS_SRC_PORT_MAX - MYA_DNS_SRC_PORT_MIN + 1u);

    if (!host || !out_ip) {
        return -1;
    }
    if (mya_net_parse_ipv4(host, out_ip) == 0) {
        return 0;
    }

    if (timeout_polls == 0u) {
        timeout_polls = MYA_DNS_TIMEOUT_POLLS_DEFAULT;
    }
    if (mya_net_default_dns_ip(&dns_ip) == 0 && dns_ip != 0u) {
        servers[server_count++] = dns_ip;
    }
    if (mya_net_read_sys_u32("net_gateway_ip", &gateway_ip) == 0 && gateway_ip != 0u) {
        uint8_t exists = 0u;
        for (uint32_t i = 0u; i < server_count; i++) {
            if (servers[i] == gateway_ip) {
                exists = 1u;
                break;
            }
        }
        if (!exists && server_count < (uint32_t)(sizeof(servers) / sizeof(servers[0]))) {
            servers[server_count++] = gateway_ip;
        }
    }
    if (server_count < (uint32_t)(sizeof(servers) / sizeof(servers[0]))) {
        servers[server_count++] = MYA_DNS_PUBLIC1;
    }
    if (server_count < (uint32_t)(sizeof(servers) / sizeof(servers[0]))) {
        servers[server_count++] = MYA_DNS_PUBLIC2;
    }
    if (server_count == 0u) {
        return -1;
    }

    pid = (uint32_t)mya_proc_getpid();
    txid = (uint16_t)((pid & 0xFFFFu) ^ 0x5A3Cu);
    if (mya_dns_build_query(host, txid, packet, &query_len) != 0) {
        return -1;
    }
    timeout_per_try = timeout_polls / (server_count * MYA_DNS_RETRIES_PER_SERVER);
    if (timeout_per_try == 0u) {
        timeout_per_try = 1u;
    }
    if (timeout_per_try > 4000u) {
        timeout_per_try = 4000u;
    }
    budget_left = timeout_polls;

    for (uint32_t retry = 0u; retry < MYA_DNS_RETRIES_PER_SERVER; retry++) {
        for (uint32_t s = 0u; s < server_count; s++) {
            uint32_t server_ip = servers[s];
            uint32_t poll_limit = timeout_per_try;
            int32_t fd = -1;
            uint16_t src_port = 0u;
            int sent;

            if (server_ip == 0u) {
                continue;
            }
            if (budget_left == 0u) {
                return -1;
            }
            if (poll_limit > budget_left) {
                poll_limit = budget_left;
            }

            for (uint32_t port_try = 0u; port_try < 64u; port_try++) {
                uint32_t mixed = pid * 131u + (s + 1u) * 977u + (retry + 1u) * 911u + port_try * 613u;
                uint32_t candidate = MYA_DNS_SRC_PORT_MIN + (mixed % span);
                if (mya_sock_open_ex(MYAOS_SOCK_PROTO_UDP, (uint16_t)candidate, &fd) == 0) {
                    src_port = (uint16_t)candidate;
                    break;
                }
            }
            if (fd < 0 || src_port == 0u) {
                continue;
            }

            {
                uint16_t req_txid = (uint16_t)(txid ^ src_port ^ (uint16_t)(retry * 257u + s * 73u));
                uint8_t local_packet[MYA_DNS_PACKET_MAX];
                uint8_t response[MYA_DNS_PACKET_MAX];
                uint32_t resp_len = 0u;
                uint16_t resp_src_port = 0u;

                for (uint32_t i = 0u; i < query_len; i++) {
                    local_packet[i] = packet[i];
                }
                mya_net_write_be16(local_packet + 0u, req_txid);

                sent = mya_net_send_udp4(server_ip, MYA_DNS_PORT, src_port, local_packet, query_len);
                if (sent >= 0 && (uint32_t)sent == query_len) {
                    for (uint32_t poll = 0u; poll < poll_limit; poll++) {
                        if (mya_sock_recvfrom(fd, response, sizeof(response), &resp_len, &resp_src_port) == 0 && resp_len > 0u) {
                            if (resp_src_port != MYA_DNS_PORT) {
                                continue;
                            }
                            if (mya_dns_parse_response(response, resp_len, req_txid, out_ip) == 0) {
                                (void)mya_sock_close(fd);
                                return 0;
                            }
                        } else {
                            mya_proc_yield();
                        }
                    }
                }
            }
            (void)mya_sock_close(fd);
            if (budget_left > poll_limit) {
                budget_left -= poll_limit;
            } else {
                budget_left = 0u;
            }
        }
    }
    return -1;
}

#endif
