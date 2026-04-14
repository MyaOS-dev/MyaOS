#include "net.h"
#include "device.h"
#include "netnic.h"
#include "timer.h"
#include <stddef.h>

#define NET_MAX_SOCKETS 32u
#define NET_UDP_QUEUE_DEPTH 8u
#define NET_UDP_MAX_PAYLOAD 512u
#define NET_TCP_RX_CAPACITY 4096u
#define NET_TCP_LISTEN_BACKLOG_MAX 16u
#define NET_EPHEMERAL_PORT_MIN 49152u
#define NET_EPHEMERAL_PORT_MAX 65535u

#define NET_ETH_HDR_SIZE 14u
#define NET_ETH_TYPE_IPV4 0x0800u
#define NET_ETH_TYPE_ARP 0x0806u
#define NET_ETH_MAX_FRAME 1518u

#define NET_IP_PROTO_ICMP 1u
#define NET_IP_PROTO_TCP 6u
#define NET_IP_PROTO_UDP 17u
#define NET_IP_HDR_SIZE 20u
#define NET_IP_MAX_PAYLOAD 1460u

#define NET_UDP_HDR_SIZE 8u
#define NET_UDP4_MAX_PAYLOAD 1400u
#define NET_TCP_HDR_SIZE 20u
#define NET_TCP_TX_MAX_PAYLOAD 1200u
#define NET_TCP_OPT_WS_LEN 4u
#define NET_TCP_DEFAULT_WS_SHIFT 2u
#define NET_TCP_RTO_TICKS 200u
#define NET_TCP_MAX_RETRANS 6u
#define NET_TCP_NAGLE_FLUSH_TICKS 8u
#define NET_TCP_KEEPALIVE_IDLE_TICKS 6000u
#define NET_TCP_KEEPALIVE_INTERVAL_TICKS 1000u
#define NET_TCP_KEEPALIVE_MAX_PROBES 3u

#define NET_ARP_PKT_SIZE 28u
#define NET_ARP_CACHE_SIZE 16u
#define NET_ARP_CACHE_TTL_TICKS 10000u
#define NET_ARP_RETRY_INTERVAL_TICKS 64u
#define NET_ARP_RETRY_MAX 5u
#define NET_ARP_PENDING_MAX 16u
#define NET_ARP_PENDING_TTL_TICKS 12000u

#define NET_DHCP_CLIENT_PORT 68u
#define NET_DHCP_SERVER_PORT 67u
#define NET_DHCP_MAGIC_COOKIE 0x63825363u
#define NET_DHCP_MSG_DISCOVER 1u
#define NET_DHCP_MSG_OFFER 2u
#define NET_DHCP_MSG_REQUEST 3u
#define NET_DHCP_MSG_ACK 5u
#define NET_DHCP_WAIT_POLLS 120000u
#define NET_TCP_CONNECT_WAIT_POLLS 120000u

#define NET_DEFAULT_SRC_IP 0x0A00020Fu
#define NET_DEFAULT_GATEWAY_IP 0x0A000202u
#define NET_DEFAULT_SUBNET_MASK 0xFFFFFF00u

typedef enum {
    NET_TCP_STATE_CLOSED = 0,
    NET_TCP_STATE_LISTEN = 1,
    NET_TCP_STATE_ESTABLISHED = 2,
    NET_TCP_STATE_SYN_RECV = 3,
    NET_TCP_STATE_SYN_SENT = 4,
} net_tcp_state_t;

typedef struct {
    uint16_t src_port;
    uint16_t len;
    uint8_t payload[NET_UDP_MAX_PAYLOAD];
} net_udp_dgram_t;

typedef struct {
    uint8_t used;
    uint8_t kind;
    uint8_t state;
    uint8_t peer_closed;
    uint8_t tcp_external;
    uint8_t reserved0;
    uint16_t reserved1;
    int32_t owner_pid;
    uint16_t local_port;
    uint16_t peer_port;
    uint32_t peer_ip;
    uint32_t tcp_tx_seq;
    uint32_t tcp_rx_next;
    int32_t peer_slot;
    int32_t listener_slot;
    uint16_t backlog;
    uint16_t accept_head;
    uint16_t accept_len;
    int16_t accept_slots[NET_TCP_LISTEN_BACKLOG_MAX];
    uint16_t udp_head;
    uint16_t udp_len;
    net_udp_dgram_t udp_queue[NET_UDP_QUEUE_DEPTH];
    uint32_t tcp_rx_head;
    uint32_t tcp_rx_len;
    uint8_t tcp_rx[NET_TCP_RX_CAPACITY];
    uint8_t tcp_peer_wscale;
    uint8_t tcp_local_wscale;
    uint8_t tcp_dup_ack_count;
    uint8_t tcp_retransmits;
    uint32_t tcp_peer_window;
    uint32_t tcp_last_ack;
    uint8_t tcp_unacked_active;
    uint8_t tcp_unacked_flags;
    uint16_t tcp_unacked_len;
    uint32_t tcp_unacked_seq;
    uint64_t tcp_unacked_deadline;
    uint8_t tcp_unacked_data[NET_TCP_TX_MAX_PAYLOAD];
    uint8_t tcp_ooo_valid;
    uint8_t tcp_ooo_reserved0;
    uint16_t tcp_ooo_len;
    uint32_t tcp_ooo_seq;
    uint8_t tcp_ooo_data[NET_TCP_TX_MAX_PAYLOAD];
    uint16_t tcp_nagle_len;
    uint16_t tcp_nagle_reserved1;
    uint64_t tcp_nagle_deadline;
    uint8_t tcp_nagle_data[NET_TCP_TX_MAX_PAYLOAD];
    uint64_t tcp_last_activity_tick;
    uint64_t tcp_keepalive_deadline;
    uint8_t tcp_keepalive_probes;
    uint8_t tcp_reserved2[7];
} net_socket_t;

typedef struct {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t dropped_packets;
} net_stats_t;

typedef struct {
    uint8_t used;
    uint8_t mac[6];
    uint32_t ip;
    uint32_t age;
} net_arp_entry_t;

typedef struct {
    uint8_t used;
    uint8_t proto;
    uint16_t payload_len;
    uint8_t payload[NET_IP_MAX_PAYLOAD];
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t next_hop_ip;
    uint8_t retries;
    uint8_t reserved0[3];
    uint64_t created_tick;
    uint64_t last_req_tick;
} net_arp_pending_t;

typedef struct {
    uint8_t mac[6];
    uint8_t has_link;
    uint8_t have_gateway_mac;
    uint8_t reserved0;
    uint32_t src_ip;
    uint32_t subnet_mask;
    uint32_t gateway_ip;
    uint32_t dns_ip;
    uint8_t gateway_mac[6];
    uint16_t ip_ident;
    uint16_t reserved1;
    uint32_t tcp_isn_seed;
} net_if_t;

typedef struct {
    uint32_t yiaddr;
    uint32_t server_id;
    uint32_t subnet_mask;
    uint32_t router;
    uint32_t dns;
} net_dhcp_offer_t;

typedef struct {
    uint8_t active;
    uint8_t matched;
    uint16_t ident;
    uint16_t seq;
    uint16_t reserved0;
    uint32_t src_ip;
    uint64_t start_tick;
    uint64_t end_tick;
} net_ping_wait_t;

static net_socket_t g_sockets[NET_MAX_SOCKETS];
static net_stats_t g_stats;
static net_arp_entry_t g_arp[NET_ARP_CACHE_SIZE];
static net_arp_pending_t g_arp_pending[NET_ARP_PENDING_MAX];
static net_if_t g_if;
static net_ping_wait_t g_ping_wait;
static uint8_t g_net_poll_active;
static uint8_t g_net_initialized;
static uint16_t g_udp4_src_port_next = NET_EPHEMERAL_PORT_MIN;

static void mem_zero(void* ptr, size_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void mem_copy(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static void mem_move(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;

    if (out == in || size == 0u) {
        return;
    }

    if (out < in) {
        for (size_t i = 0; i < size; i++) {
            out[i] = in[i];
        }
    } else {
        for (size_t i = size; i > 0u; i--) {
            out[i - 1u] = in[i - 1u];
        }
    }
}

static int mem_eq(const void* a, const void* b, size_t size) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < size; i++) {
        if (pa[i] != pb[i]) {
            return 0;
        }
    }
    return 1;
}

static void cpu_relax(void) {
    __asm__ __volatile__("pause");
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static int tcp_seq_ge(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b) >= 0) ? 1 : 0;
}

static int tcp_seq_lt(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b) < 0) ? 1 : 0;
}

static uint16_t read_be16(const uint8_t* src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t read_be32(const uint8_t* src) {
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static void write_be16(uint8_t* dst, uint16_t value) {
    if (!dst) {
        return;
    }
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static void write_be32(uint8_t* dst, uint32_t value) {
    if (!dst) {
        return;
    }
    dst[0] = (uint8_t)((value >> 24) & 0xFFu);
    dst[1] = (uint8_t)((value >> 16) & 0xFFu);
    dst[2] = (uint8_t)((value >> 8) & 0xFFu);
    dst[3] = (uint8_t)(value & 0xFFu);
}

static uint16_t ip_checksum16(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0u;

    if (!data || len == 0u) {
        return 0u;
    }

    for (uint32_t i = 0; i + 1u < len; i += 2u) {
        uint16_t word = (uint16_t)(((uint16_t)data[i] << 8) | data[i + 1u]);
        sum += word;
    }
    if ((len & 1u) != 0u) {
        sum += (uint16_t)((uint16_t)data[len - 1u] << 8);
    }

    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

static uint16_t tcp_checksum_ipv4(uint32_t src_ip, uint32_t dst_ip, const uint8_t* tcp, uint16_t tcp_len) {
    uint32_t sum = 0u;

    sum += (src_ip >> 16) & 0xFFFFu;
    sum += src_ip & 0xFFFFu;
    sum += (dst_ip >> 16) & 0xFFFFu;
    sum += dst_ip & 0xFFFFu;
    sum += (uint16_t)NET_IP_PROTO_TCP;
    sum += tcp_len;

    for (uint16_t i = 0; i + 1u < tcp_len; i += 2u) {
        sum += (uint16_t)(((uint16_t)tcp[i] << 8) | tcp[i + 1u]);
    }
    if ((tcp_len & 1u) != 0u) {
        sum += (uint16_t)((uint16_t)tcp[tcp_len - 1u] << 8);
    }

    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum & 0xFFFFu);
}

static uint16_t udp_checksum_ipv4(uint32_t src_ip, uint32_t dst_ip, const uint8_t* udp, uint16_t udp_len) {
    uint32_t sum = 0u;
    uint16_t out;

    if (!udp || udp_len == 0u) {
        return 0u;
    }

    sum += (src_ip >> 16) & 0xFFFFu;
    sum += src_ip & 0xFFFFu;
    sum += (dst_ip >> 16) & 0xFFFFu;
    sum += dst_ip & 0xFFFFu;
    sum += (uint16_t)NET_IP_PROTO_UDP;
    sum += udp_len;

    for (uint16_t i = 0u; i + 1u < udp_len; i += 2u) {
        sum += (uint16_t)(((uint16_t)udp[i] << 8) | udp[i + 1u]);
    }
    if ((udp_len & 1u) != 0u) {
        sum += (uint16_t)((uint16_t)udp[udp_len - 1u] << 8);
    }

    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    out = (uint16_t)(~sum & 0xFFFFu);
    return (out == 0u) ? 0xFFFFu : out;
}

static int ip_is_broadcast(uint32_t ip) {
    return ip == 0xFFFFFFFFu;
}

static int ip_same_subnet(uint32_t a, uint32_t b, uint32_t mask) {
    return ((a & mask) == (b & mask)) ? 1 : 0;
}

static int socket_slot_valid(uint32_t slot) {
    return (slot < NET_MAX_SOCKETS && g_sockets[slot].used) ? 1 : 0;
}

static void accept_queue_reset(net_socket_t* sock) {
    if (!sock) {
        return;
    }

    sock->accept_head = 0u;
    sock->accept_len = 0u;
    for (uint32_t i = 0; i < NET_TCP_LISTEN_BACKLOG_MAX; i++) {
        sock->accept_slots[i] = -1;
    }
}

static int find_free_socket_slot(void) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static net_socket_t* socket_from_fd(int32_t fd) {
    uint32_t slot;

    if (fd <= 0) {
        return NULL;
    }
    slot = (uint32_t)(fd - 1);
    if (!socket_slot_valid(slot)) {
        return NULL;
    }
    return &g_sockets[slot];
}

static net_socket_t* socket_for_owner(int32_t owner_pid, int32_t fd) {
    net_socket_t* sock = socket_from_fd(fd);

    if (!sock || sock->owner_pid != owner_pid) {
        return NULL;
    }
    return sock;
}

static int udp_port_in_use(uint16_t port, int32_t ignore_slot) {
    if (port == 0u) {
        return 1;
    }

    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].used || g_sockets[i].kind != MYAOS_SOCK_PROTO_UDP) {
            continue;
        }
        if ((int32_t)i == ignore_slot) {
            continue;
        }
        if (g_sockets[i].local_port == port) {
            return 1;
        }
    }

    return 0;
}

static int tcp_port_reserved(uint16_t port, int32_t ignore_slot) {
    if (port == 0u) {
        return 1;
    }

    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        net_socket_t* it = &g_sockets[i];

        if (!it->used || it->kind != MYAOS_SOCK_PROTO_TCP) {
            continue;
        }
        if ((int32_t)i == ignore_slot) {
            continue;
        }
        if (it->local_port != port) {
            continue;
        }

        if (it->state == NET_TCP_STATE_ESTABLISHED && it->listener_slot >= 0) {
            continue;
        }
        return 1;
    }

    return 0;
}

static int tcp_tuple_in_use(uint16_t local_port, uint16_t peer_port, int32_t ignore_slot) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        net_socket_t* it = &g_sockets[i];

        if (!it->used || it->kind != MYAOS_SOCK_PROTO_TCP || it->state != NET_TCP_STATE_ESTABLISHED || it->tcp_external) {
            continue;
        }
        if ((int32_t)i == ignore_slot) {
            continue;
        }
        if (it->local_port == local_port && it->peer_port == peer_port) {
            return 1;
        }
    }

    return 0;
}

static uint16_t pick_udp_ephemeral_port(void) {
    for (uint32_t p = NET_EPHEMERAL_PORT_MIN; p <= NET_EPHEMERAL_PORT_MAX; p++) {
        if (!udp_port_in_use((uint16_t)p, -1)) {
            return (uint16_t)p;
        }
    }
    return 0u;
}

static uint16_t pick_tcp_ephemeral_port(void) {
    for (uint32_t p = NET_EPHEMERAL_PORT_MIN; p <= NET_EPHEMERAL_PORT_MAX; p++) {
        if (!tcp_port_reserved((uint16_t)p, -1)) {
            return (uint16_t)p;
        }
    }
    return 0u;
}

static int find_tcp_listener_slot(uint16_t port) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].used || g_sockets[i].kind != MYAOS_SOCK_PROTO_TCP) {
            continue;
        }
        if (g_sockets[i].state == NET_TCP_STATE_LISTEN && g_sockets[i].local_port == port) {
            return (int)i;
        }
    }
    return -1;
}

static int find_external_tcp_slot(uint16_t local_port, uint32_t peer_ip, uint16_t peer_port) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        net_socket_t* sock = &g_sockets[i];

        if (!sock->used || sock->kind != MYAOS_SOCK_PROTO_TCP || !sock->tcp_external) {
            continue;
        }
        if (sock->local_port == local_port && sock->peer_port == peer_port && sock->peer_ip == peer_ip) {
            return (int)i;
        }
    }
    return -1;
}

static int accept_queue_push(net_socket_t* listener, int32_t child_slot) {
    uint32_t idx;

    if (!listener || listener->backlog == 0u || listener->backlog > NET_TCP_LISTEN_BACKLOG_MAX) {
        return -1;
    }
    if (listener->accept_len >= listener->backlog) {
        return -1;
    }

    idx = (listener->accept_head + listener->accept_len) % NET_TCP_LISTEN_BACKLOG_MAX;
    listener->accept_slots[idx] = (int16_t)child_slot;
    listener->accept_len++;
    return 0;
}

static int accept_queue_pop(net_socket_t* listener) {
    uint32_t idx;
    int16_t child_slot;

    if (!listener || listener->accept_len == 0u) {
        return -1;
    }

    idx = listener->accept_head;
    child_slot = listener->accept_slots[idx];
    listener->accept_slots[idx] = -1;
    listener->accept_head = (listener->accept_head + 1u) % NET_TCP_LISTEN_BACKLOG_MAX;
    listener->accept_len--;
    return (int)child_slot;
}

static void accept_queue_remove_slot(net_socket_t* listener, int32_t child_slot) {
    int16_t kept[NET_TCP_LISTEN_BACKLOG_MAX];
    uint16_t kept_len = 0u;

    if (!listener) {
        return;
    }

    for (uint32_t i = 0; i < NET_TCP_LISTEN_BACKLOG_MAX; i++) {
        kept[i] = -1;
    }

    for (uint32_t i = 0; i < listener->accept_len; i++) {
        uint32_t idx = (listener->accept_head + i) % NET_TCP_LISTEN_BACKLOG_MAX;
        int16_t slot = listener->accept_slots[idx];

        if (slot < 0 || slot == (int16_t)child_slot) {
            continue;
        }
        kept[kept_len++] = slot;
    }

    accept_queue_reset(listener);
    for (uint32_t i = 0; i < kept_len; i++) {
        listener->accept_slots[i] = kept[i];
    }
    listener->accept_len = kept_len;
}

static uint32_t tcp_rx_write(net_socket_t* sock, const uint8_t* data, uint32_t len) {
    uint32_t space;
    uint32_t copy_len;
    uint32_t tail;

    if (!sock || (!data && len != 0u) || len == 0u) {
        return 0u;
    }

    space = NET_TCP_RX_CAPACITY - sock->tcp_rx_len;
    copy_len = min_u32(len, space);
    if (copy_len == 0u) {
        return 0u;
    }

    tail = (sock->tcp_rx_head + sock->tcp_rx_len) % NET_TCP_RX_CAPACITY;
    if (tail + copy_len <= NET_TCP_RX_CAPACITY) {
        mem_copy(&sock->tcp_rx[tail], data, copy_len);
    } else {
        uint32_t first = NET_TCP_RX_CAPACITY - tail;
        uint32_t second = copy_len - first;

        mem_copy(&sock->tcp_rx[tail], data, first);
        mem_copy(&sock->tcp_rx[0], data + first, second);
    }

    sock->tcp_rx_len += copy_len;
    return copy_len;
}

static uint32_t tcp_rx_read(net_socket_t* sock, uint8_t* out, uint32_t max_len) {
    uint32_t copy_len;

    if (!sock || (!out && max_len != 0u) || max_len == 0u || sock->tcp_rx_len == 0u) {
        return 0u;
    }

    copy_len = min_u32(max_len, sock->tcp_rx_len);
    if (sock->tcp_rx_head + copy_len <= NET_TCP_RX_CAPACITY) {
        mem_copy(out, &sock->tcp_rx[sock->tcp_rx_head], copy_len);
    } else {
        uint32_t first = NET_TCP_RX_CAPACITY - sock->tcp_rx_head;
        uint32_t second = copy_len - first;

        mem_copy(out, &sock->tcp_rx[sock->tcp_rx_head], first);
        mem_copy(out + first, &sock->tcp_rx[0], second);
    }

    sock->tcp_rx_head = (sock->tcp_rx_head + copy_len) % NET_TCP_RX_CAPACITY;
    sock->tcp_rx_len -= copy_len;
    return copy_len;
}

static int create_socket_slot(int32_t owner_pid, uint8_t proto, int32_t* out_slot) {
    int slot;
    net_socket_t* sock;

    if (!out_slot || owner_pid <= 0) {
        return -1;
    }

    slot = find_free_socket_slot();
    if (slot < 0) {
        return -1;
    }

    sock = &g_sockets[slot];
    mem_zero(sock, sizeof(*sock));
    sock->used = 1u;
    sock->kind = proto;
    sock->state = NET_TCP_STATE_CLOSED;
    sock->owner_pid = owner_pid;
    sock->peer_slot = -1;
    sock->listener_slot = -1;
    sock->backlog = 1u;
    sock->tcp_local_wscale = NET_TCP_DEFAULT_WS_SHIFT;
    sock->tcp_peer_wscale = 0u;
    sock->tcp_peer_window = NET_TCP_TX_MAX_PAYLOAD;
    sock->tcp_last_ack = 0u;
    sock->tcp_keepalive_deadline = timer_ticks() + NET_TCP_KEEPALIVE_IDLE_TICKS;
    accept_queue_reset(sock);
    *out_slot = slot;
    return 0;
}

static void tcp_mark_peer_closed(int32_t peer_slot, int32_t closed_slot) {
    net_socket_t* peer;

    if (peer_slot < 0 || !socket_slot_valid((uint32_t)peer_slot)) {
        return;
    }

    peer = &g_sockets[peer_slot];
    if (peer->kind != MYAOS_SOCK_PROTO_TCP) {
        return;
    }

    if (peer->peer_slot == closed_slot) {
        peer->peer_slot = -1;
    }
    peer->peer_closed = 1u;
}

static void close_socket_slot(uint32_t slot) {
    net_socket_t* sock;

    if (!socket_slot_valid(slot)) {
        return;
    }

    sock = &g_sockets[slot];

    if (sock->kind == MYAOS_SOCK_PROTO_TCP) {
        if (sock->state == NET_TCP_STATE_LISTEN) {
            for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
                net_socket_t* child = &g_sockets[i];

                if (!child->used || child->kind != MYAOS_SOCK_PROTO_TCP) {
                    continue;
                }
                if (child->listener_slot != (int32_t)slot) {
                    continue;
                }

                tcp_mark_peer_closed(child->peer_slot, (int32_t)i);
                mem_zero(child, sizeof(*child));
            }
        } else if (sock->state == NET_TCP_STATE_ESTABLISHED || sock->state == NET_TCP_STATE_SYN_RECV) {
            tcp_mark_peer_closed(sock->peer_slot, (int32_t)slot);
            if (sock->listener_slot >= 0 && socket_slot_valid((uint32_t)sock->listener_slot)) {
                accept_queue_remove_slot(&g_sockets[sock->listener_slot], (int32_t)slot);
            }
        }
    }

    mem_zero(sock, sizeof(*sock));
}

static int net_device_sync(device_t* dev) {
    (void)dev;
    return 0;
}

static const device_ops_t g_net_device_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = net_device_sync,
};

static int send_ipv4_packet_to_mac(
    uint32_t src_ip,
    uint32_t dst_ip,
    uint8_t proto,
    const uint8_t* payload,
    uint16_t payload_len,
    const uint8_t dst_mac[6]
);
static int arp_send_request(uint32_t target_ip);

static int arp_entry_expired(const net_arp_entry_t* entry, uint32_t now32) {
    if (!entry || !entry->used) {
        return 1;
    }
    return ((uint32_t)(now32 - entry->age) > NET_ARP_CACHE_TTL_TICKS) ? 1 : 0;
}

static void arp_cache_reset(void) {
    for (uint32_t i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        mem_zero(&g_arp[i], sizeof(g_arp[i]));
    }
}

static void arp_pending_reset(void) {
    for (uint32_t i = 0; i < NET_ARP_PENDING_MAX; i++) {
        mem_zero(&g_arp_pending[i], sizeof(g_arp_pending[i]));
    }
}

static void arp_cache_sweep_expired(void) {
    uint32_t now32 = (uint32_t)timer_ticks();

    for (uint32_t i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        if (!g_arp[i].used) {
            continue;
        }
        if (!arp_entry_expired(&g_arp[i], now32)) {
            continue;
        }
        if (g_arp[i].ip == g_if.gateway_ip) {
            g_if.have_gateway_mac = 0u;
        }
        mem_zero(&g_arp[i], sizeof(g_arp[i]));
    }
}

static void arp_learn(uint32_t ip, const uint8_t mac[6]) {
    int free_slot = -1;
    uint32_t oldest_age = 0u;
    int oldest_slot = 0;
    uint32_t now32 = (uint32_t)timer_ticks();

    if (!mac || ip == 0u || ip_is_broadcast(ip)) {
        return;
    }

    for (uint32_t i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        if (g_arp[i].used) {
            if (arp_entry_expired(&g_arp[i], now32)) {
                if (free_slot < 0) {
                    free_slot = (int)i;
                }
                continue;
            }
            if (g_arp[i].ip == ip) {
                mem_copy(g_arp[i].mac, mac, 6u);
                g_arp[i].age = now32;
                return;
            }
            if (g_arp[i].age <= oldest_age || oldest_age == 0u) {
                oldest_age = g_arp[i].age;
                oldest_slot = (int)i;
            }
        } else if (free_slot < 0) {
            free_slot = (int)i;
        }
    }

    if (free_slot < 0) {
        free_slot = oldest_slot;
    }

    g_arp[free_slot].used = 1u;
    g_arp[free_slot].ip = ip;
    mem_copy(g_arp[free_slot].mac, mac, 6u);
    g_arp[free_slot].age = now32;

    if (ip == g_if.gateway_ip) {
        mem_copy(g_if.gateway_mac, mac, 6u);
        g_if.have_gateway_mac = 1u;
    }
}

static int arp_lookup(uint32_t ip, uint8_t out_mac[6]) {
    uint32_t now32 = (uint32_t)timer_ticks();

    for (uint32_t i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        if (!g_arp[i].used || g_arp[i].ip != ip) {
            continue;
        }
        if (arp_entry_expired(&g_arp[i], now32)) {
            if (g_arp[i].ip == g_if.gateway_ip) {
                g_if.have_gateway_mac = 0u;
            }
            mem_zero(&g_arp[i], sizeof(g_arp[i]));
            continue;
        }
        if (out_mac) {
            mem_copy(out_mac, g_arp[i].mac, 6u);
        }
        g_arp[i].age = now32;
        return 0;
    }
    return -1;
}

static int arp_pending_enqueue(
    uint32_t src_ip,
    uint32_t dst_ip,
    uint32_t next_hop_ip,
    uint8_t proto,
    const uint8_t* payload,
    uint16_t payload_len
) {
    int free_slot = -1;
    int oldest_slot = 0;
    uint64_t oldest_tick = 0u;
    uint64_t now = timer_ticks();
    net_arp_pending_t* slot;

    if (!payload || payload_len == 0u || payload_len > NET_IP_MAX_PAYLOAD || next_hop_ip == 0u) {
        return -1;
    }

    for (uint32_t i = 0; i < NET_ARP_PENDING_MAX; i++) {
        if (g_arp_pending[i].used && g_arp_pending[i].src_ip == src_ip && g_arp_pending[i].dst_ip == dst_ip &&
            g_arp_pending[i].next_hop_ip == next_hop_ip && g_arp_pending[i].proto == proto &&
            g_arp_pending[i].payload_len == payload_len && mem_eq(g_arp_pending[i].payload, payload, payload_len)) {
            return 0;
        }
        if (!g_arp_pending[i].used) {
            free_slot = (int)i;
            break;
        }
        if (g_arp_pending[i].created_tick <= oldest_tick || oldest_tick == 0u) {
            oldest_tick = g_arp_pending[i].created_tick;
            oldest_slot = (int)i;
        }
    }

    if (free_slot < 0) {
        free_slot = oldest_slot;
        g_stats.dropped_packets++;
    }

    slot = &g_arp_pending[free_slot];
    mem_zero(slot, sizeof(*slot));
    slot->used = 1u;
    slot->proto = proto;
    slot->payload_len = payload_len;
    slot->src_ip = src_ip;
    slot->dst_ip = dst_ip;
    slot->next_hop_ip = next_hop_ip;
    slot->retries = 1u;
    slot->created_tick = now;
    slot->last_req_tick = now;
    mem_copy(slot->payload, payload, payload_len);
    return 0;
}

static void arp_pending_kick(void) {
    uint8_t mac[6];
    uint64_t now = timer_ticks();

    for (uint32_t i = 0; i < NET_ARP_PENDING_MAX; i++) {
        net_arp_pending_t* p = &g_arp_pending[i];

        if (!p->used) {
            continue;
        }
        if ((uint64_t)(now - p->created_tick) > NET_ARP_PENDING_TTL_TICKS) {
            mem_zero(p, sizeof(*p));
            g_stats.dropped_packets++;
            continue;
        }

        if (arp_lookup(p->next_hop_ip, mac) == 0) {
            if (send_ipv4_packet_to_mac(p->src_ip, p->dst_ip, p->proto, p->payload, p->payload_len, mac) == 0) {
                mem_zero(p, sizeof(*p));
            }
            continue;
        }

        if ((uint64_t)(now - p->last_req_tick) < NET_ARP_RETRY_INTERVAL_TICKS) {
            continue;
        }
        if (p->retries >= NET_ARP_RETRY_MAX) {
            mem_zero(p, sizeof(*p));
            g_stats.dropped_packets++;
            continue;
        }
        if (arp_send_request(p->next_hop_ip) == 0) {
            p->retries++;
            p->last_req_tick = now;
        }
    }
}

static int send_ethernet(const uint8_t dst_mac[6], uint16_t eth_type, const void* payload, uint16_t payload_len) {
    uint8_t frame[NET_ETH_MAX_FRAME];

    if (!dst_mac || !payload || payload_len == 0u || !g_if.has_link) {
        return -1;
    }
    if (payload_len + NET_ETH_HDR_SIZE > sizeof(frame)) {
        return -1;
    }

    mem_copy(&frame[0], dst_mac, 6u);
    mem_copy(&frame[6], g_if.mac, 6u);
    frame[12] = (uint8_t)(eth_type >> 8);
    frame[13] = (uint8_t)(eth_type & 0xFFu);
    mem_copy(&frame[NET_ETH_HDR_SIZE], payload, payload_len);

    if (netnic_send_frame(frame, (uint16_t)(NET_ETH_HDR_SIZE + payload_len)) != 0) {
        g_stats.dropped_packets++;
        return -1;
    }

    g_stats.tx_packets++;
    return 0;
}

static int arp_send_request(uint32_t target_ip) {
    uint8_t pkt[NET_ARP_PKT_SIZE];
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    if (!g_if.has_link || g_if.src_ip == 0u) {
        return -1;
    }

    write_be16(&pkt[0], 1u);
    write_be16(&pkt[2], NET_ETH_TYPE_IPV4);
    pkt[4] = 6u;
    pkt[5] = 4u;
    write_be16(&pkt[6], 1u);
    mem_copy(&pkt[8], g_if.mac, 6u);
    write_be32(&pkt[14], g_if.src_ip);
    mem_zero(&pkt[18], 6u);
    write_be32(&pkt[24], target_ip);

    return send_ethernet(bcast, NET_ETH_TYPE_ARP, pkt, sizeof(pkt));
}

static int arp_send_reply(const uint8_t target_mac[6], uint32_t target_ip) {
    uint8_t pkt[NET_ARP_PKT_SIZE];

    if (!target_mac || !g_if.has_link || g_if.src_ip == 0u) {
        return -1;
    }

    write_be16(&pkt[0], 1u);
    write_be16(&pkt[2], NET_ETH_TYPE_IPV4);
    pkt[4] = 6u;
    pkt[5] = 4u;
    write_be16(&pkt[6], 2u);
    mem_copy(&pkt[8], g_if.mac, 6u);
    write_be32(&pkt[14], g_if.src_ip);
    mem_copy(&pkt[18], target_mac, 6u);
    write_be32(&pkt[24], target_ip);

    return send_ethernet(target_mac, NET_ETH_TYPE_ARP, pkt, sizeof(pkt));
}

static void net_handle_frame(const uint8_t* frame, uint16_t len);
static void tcp_housekeeping(void);

static void net_poll_rx(uint32_t budget) {
    uint8_t frame[NET_ETH_MAX_FRAME];
    uint8_t owner = 0u;

    if (!g_net_poll_active) {
        g_net_poll_active = 1u;
        owner = 1u;
        arp_cache_sweep_expired();
        tcp_housekeeping();
        arp_pending_kick();
    }

    for (uint32_t i = 0; i < budget; i++) {
        uint16_t len = 0u;
        if (netnic_poll_frame(frame, sizeof(frame), &len) != 0) {
            break;
        }
        if (len < NET_ETH_HDR_SIZE) {
            continue;
        }
        g_stats.rx_packets++;
        net_handle_frame(frame, len);
    }

    if (owner) {
        arp_cache_sweep_expired();
        arp_pending_kick();
        g_net_poll_active = 0u;
    }
}

static int arp_resolve(uint32_t ip, uint8_t out_mac[6]) {
    if (ip_is_broadcast(ip)) {
        static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
        mem_copy(out_mac, bcast, 6u);
        return 0;
    }

    if (arp_lookup(ip, out_mac) == 0) {
        return 0;
    }

    if (arp_send_request(ip) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < 4096u; i++) {
        net_poll_rx(8u);
        if (arp_lookup(ip, out_mac) == 0) {
            return 0;
        }
        if ((i & 255u) == 255u) {
            (void)arp_send_request(ip);
        }
        cpu_relax();
    }

    return -1;
}

static int send_ipv4_packet_to_mac(
    uint32_t src_ip,
    uint32_t dst_ip,
    uint8_t proto,
    const uint8_t* payload,
    uint16_t payload_len,
    const uint8_t dst_mac[6]
) {
    uint8_t pkt[NET_IP_HDR_SIZE + NET_IP_MAX_PAYLOAD];
    uint16_t total_len;

    if (!dst_mac || !payload || payload_len == 0u || payload_len > NET_IP_MAX_PAYLOAD || !g_if.has_link) {
        return -1;
    }

    total_len = (uint16_t)(NET_IP_HDR_SIZE + payload_len);
    pkt[0] = 0x45u;
    pkt[1] = 0u;
    write_be16(&pkt[2], total_len);
    write_be16(&pkt[4], g_if.ip_ident++);
    write_be16(&pkt[6], 0u);
    pkt[8] = 64u;
    pkt[9] = proto;
    write_be16(&pkt[10], 0u);
    write_be32(&pkt[12], src_ip);
    write_be32(&pkt[16], dst_ip);
    write_be16(&pkt[10], ip_checksum16(pkt, NET_IP_HDR_SIZE));
    mem_copy(&pkt[NET_IP_HDR_SIZE], payload, payload_len);

    return send_ethernet(dst_mac, NET_ETH_TYPE_IPV4, pkt, total_len);
}

static int send_ipv4_packet(
    uint32_t src_ip,
    uint32_t dst_ip,
    uint8_t proto,
    const uint8_t* payload,
    uint16_t payload_len,
    const uint8_t* forced_dst_mac
) {
    uint8_t dst_mac[6];
    uint32_t next_hop;
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    if (!payload || payload_len == 0u || payload_len > NET_IP_MAX_PAYLOAD || !g_if.has_link) {
        return -1;
    }

    if (forced_dst_mac) {
        return send_ipv4_packet_to_mac(src_ip, dst_ip, proto, payload, payload_len, forced_dst_mac);
    }
    if (ip_is_broadcast(dst_ip)) {
        return send_ipv4_packet_to_mac(src_ip, dst_ip, proto, payload, payload_len, bcast);
    }

    next_hop = ip_same_subnet(src_ip, dst_ip, g_if.subnet_mask) ? dst_ip : g_if.gateway_ip;
    if (next_hop == 0u) {
        return -1;
    }

    if (arp_lookup(next_hop, dst_mac) == 0) {
        return send_ipv4_packet_to_mac(src_ip, dst_ip, proto, payload, payload_len, dst_mac);
    }

    if (arp_pending_enqueue(src_ip, dst_ip, next_hop, proto, payload, payload_len) != 0) {
        g_stats.dropped_packets++;
        return -1;
    }

    if (arp_send_request(next_hop) != 0) {
        return -1;
    }

    return 0;
}

static int send_udp_ipv4(
    uint32_t src_ip,
    uint32_t dst_ip,
    uint16_t src_port,
    uint16_t dst_port,
    const uint8_t* data,
    uint16_t len,
    const uint8_t* forced_dst_mac
) {
    uint8_t udp[NET_UDP_HDR_SIZE + NET_UDP4_MAX_PAYLOAD];
    uint16_t udp_len;

    if (dst_port == 0u || len > NET_UDP4_MAX_PAYLOAD || (len != 0u && !data)) {
        return -1;
    }

    udp_len = (uint16_t)(NET_UDP_HDR_SIZE + len);
    write_be16(&udp[0], src_port);
    write_be16(&udp[2], dst_port);
    write_be16(&udp[4], udp_len);
    write_be16(&udp[6], 0u);
    if (len > 0u) {
        mem_copy(&udp[NET_UDP_HDR_SIZE], data, len);
    }
    write_be16(&udp[6], udp_checksum_ipv4(src_ip, dst_ip, udp, udp_len));

    return send_ipv4_packet(src_ip, dst_ip, NET_IP_PROTO_UDP, udp, udp_len, forced_dst_mac);
}

static uint32_t tcp_segment_consumed(uint8_t flags, uint16_t len) {
    uint32_t consumed = (uint32_t)len;
    if ((flags & 0x02u) != 0u) {
        consumed++;
    }
    if ((flags & 0x01u) != 0u) {
        consumed++;
    }
    return consumed;
}

static int tcp_parse_window_scale_opt(const uint8_t* tcp, uint8_t hdr_len, uint8_t* out_shift) {
    uint8_t pos = NET_TCP_HDR_SIZE;

    if (!tcp || !out_shift || hdr_len <= NET_TCP_HDR_SIZE) {
        return -1;
    }

    while (pos < hdr_len) {
        uint8_t kind = tcp[pos];

        if (kind == 0u) {
            break;
        }
        if (kind == 1u) {
            pos++;
            continue;
        }
        if (pos + 1u >= hdr_len) {
            break;
        }

        {
            uint8_t opt_len = tcp[pos + 1u];
            if (opt_len < 2u || pos + opt_len > hdr_len) {
                break;
            }
            if (kind == 3u && opt_len == 3u) {
                uint8_t shift = tcp[pos + 2u];
                if (shift > 14u) {
                    shift = 14u;
                }
                *out_shift = shift;
                return 0;
            }
            pos = (uint8_t)(pos + opt_len);
        }
    }

    return -1;
}

static void tcp_mark_activity(net_socket_t* sock) {
    uint64_t now;

    if (!sock) {
        return;
    }

    now = timer_ticks();
    sock->tcp_last_activity_tick = now;
    sock->tcp_keepalive_deadline = now + NET_TCP_KEEPALIVE_IDLE_TICKS;
    sock->tcp_keepalive_probes = 0u;
}

static void tcp_clear_unacked(net_socket_t* sock) {
    if (!sock) {
        return;
    }

    sock->tcp_unacked_active = 0u;
    sock->tcp_unacked_flags = 0u;
    sock->tcp_unacked_len = 0u;
    sock->tcp_unacked_seq = 0u;
    sock->tcp_unacked_deadline = 0u;
    sock->tcp_dup_ack_count = 0u;
    sock->tcp_retransmits = 0u;
}

static int tcp_send_segment_internal(
    net_socket_t* sock,
    uint8_t flags,
    const uint8_t* data,
    uint16_t len,
    uint8_t retransmit,
    uint8_t force_seq,
    uint32_t forced_seq
) {
    uint8_t seg[NET_TCP_HDR_SIZE + NET_TCP_OPT_WS_LEN + NET_TCP_TX_MAX_PAYLOAD];
    uint16_t hdr_len = NET_TCP_HDR_SIZE;
    uint16_t tcp_len;
    uint32_t seq_no;
    uint32_t ack_no;
    uint32_t consumed;
    uint32_t rx_space;
    uint32_t adv_window;
    uint8_t ws_shift;

    if (!sock || !sock->tcp_external || sock->peer_ip == 0u) {
        return -1;
    }
    if (len > NET_TCP_TX_MAX_PAYLOAD || (len != 0u && !data)) {
        return -1;
    }

    if ((flags & 0x02u) != 0u) {
        hdr_len = (uint16_t)(NET_TCP_HDR_SIZE + NET_TCP_OPT_WS_LEN);
    }

    seq_no = force_seq ? forced_seq : sock->tcp_tx_seq;
    ack_no = sock->tcp_rx_next;
    tcp_len = (uint16_t)(hdr_len + len);

    write_be16(&seg[0], sock->local_port);
    write_be16(&seg[2], sock->peer_port);
    write_be32(&seg[4], seq_no);
    write_be32(&seg[8], ack_no);
    seg[12] = (uint8_t)((hdr_len / 4u) << 4);
    seg[13] = flags;

    rx_space = NET_TCP_RX_CAPACITY - min_u32(sock->tcp_rx_len, NET_TCP_RX_CAPACITY);
    ws_shift = sock->tcp_local_wscale;
    if (ws_shift > 14u) {
        ws_shift = 14u;
    }
    adv_window = rx_space >> ws_shift;
    if (adv_window == 0u && rx_space > 0u) {
        adv_window = 1u;
    }
    if (adv_window > 0xFFFFu) {
        adv_window = 0xFFFFu;
    }
    write_be16(&seg[14], (uint16_t)adv_window);
    write_be16(&seg[16], 0u);
    write_be16(&seg[18], 0u);

    if ((flags & 0x02u) != 0u) {
        seg[20] = 1u; /* NOP */
        seg[21] = 3u; /* WS option */
        seg[22] = 3u;
        seg[23] = ws_shift;
    }
    if (len > 0u) {
        mem_copy(&seg[hdr_len], data, len);
    }

    write_be16(&seg[16], tcp_checksum_ipv4(g_if.src_ip, sock->peer_ip, seg, tcp_len));

    if (send_ipv4_packet(g_if.src_ip, sock->peer_ip, NET_IP_PROTO_TCP, seg, tcp_len, NULL) != 0) {
        return -1;
    }

    consumed = tcp_segment_consumed(flags, len);
    if (!retransmit) {
        if (consumed > 0u) {
            sock->tcp_unacked_active = 1u;
            sock->tcp_unacked_flags = flags;
            sock->tcp_unacked_len = len;
            sock->tcp_unacked_seq = seq_no;
            if (len > 0u) {
                mem_copy(sock->tcp_unacked_data, data, len);
            }
            sock->tcp_unacked_deadline = timer_ticks() + NET_TCP_RTO_TICKS;
            sock->tcp_retransmits = 0u;
            sock->tcp_last_ack = seq_no;
            sock->tcp_dup_ack_count = 0u;
            sock->tcp_tx_seq += consumed;
        }
    } else if (consumed > 0u) {
        sock->tcp_unacked_deadline = timer_ticks() + NET_TCP_RTO_TICKS;
    }

    tcp_mark_activity(sock);
    return 0;
}

static int tcp_send_segment(net_socket_t* sock, uint8_t flags, const uint8_t* data, uint16_t len) {
    return tcp_send_segment_internal(sock, flags, data, len, 0u, 0u, 0u);
}

static int tcp_retransmit_unacked(net_socket_t* sock) {
    if (!sock || !sock->tcp_unacked_active) {
        return -1;
    }

    if (tcp_send_segment_internal(
            sock,
            sock->tcp_unacked_flags,
            sock->tcp_unacked_data,
            sock->tcp_unacked_len,
            1u,
            1u,
            sock->tcp_unacked_seq
        ) != 0) {
        return -1;
    }

    if (sock->tcp_retransmits < 0xFFu) {
        sock->tcp_retransmits++;
    }
    return 0;
}

static uint32_t tcp_queue_nagle(net_socket_t* sock, const uint8_t* data, uint32_t len) {
    uint32_t space;
    uint32_t copy_len;

    if (!sock || !data || len == 0u) {
        return 0u;
    }

    space = NET_TCP_TX_MAX_PAYLOAD - sock->tcp_nagle_len;
    copy_len = min_u32(space, len);
    if (copy_len == 0u) {
        return 0u;
    }

    mem_copy(&sock->tcp_nagle_data[sock->tcp_nagle_len], data, copy_len);
    sock->tcp_nagle_len = (uint16_t)(sock->tcp_nagle_len + copy_len);
    sock->tcp_nagle_deadline = timer_ticks() + NET_TCP_NAGLE_FLUSH_TICKS;
    return copy_len;
}

static int tcp_try_flush_nagle(net_socket_t* sock) {
    uint16_t send_len;
    uint16_t left;

    if (!sock || sock->tcp_nagle_len == 0u || sock->tcp_unacked_active) {
        return -1;
    }

    send_len = sock->tcp_nagle_len;
    if (sock->tcp_peer_window != 0u && send_len > sock->tcp_peer_window) {
        send_len = (uint16_t)sock->tcp_peer_window;
    }
    if (send_len == 0u) {
        return -1;
    }

    if (tcp_send_segment(sock, 0x18u, sock->tcp_nagle_data, send_len) != 0) {
        return -1;
    }

    left = (uint16_t)(sock->tcp_nagle_len - send_len);
    if (left > 0u) {
        mem_move(&sock->tcp_nagle_data[0], &sock->tcp_nagle_data[send_len], left);
        sock->tcp_nagle_deadline = timer_ticks() + NET_TCP_NAGLE_FLUSH_TICKS;
    } else {
        sock->tcp_nagle_deadline = 0u;
    }
    sock->tcp_nagle_len = left;
    return 0;
}

static int tcp_send_keepalive(net_socket_t* sock) {
    uint32_t seq_no;
    uint64_t last_activity;
    uint64_t keepalive_deadline;
    uint8_t keepalive_probes;
    int rc;

    if (!sock) {
        return -1;
    }

    seq_no = (sock->tcp_tx_seq > 0u) ? (sock->tcp_tx_seq - 1u) : 0u;
    last_activity = sock->tcp_last_activity_tick;
    keepalive_deadline = sock->tcp_keepalive_deadline;
    keepalive_probes = sock->tcp_keepalive_probes;
    rc = tcp_send_segment_internal(sock, 0x10u, NULL, 0u, 1u, 1u, seq_no);
    sock->tcp_last_activity_tick = last_activity;
    sock->tcp_keepalive_deadline = keepalive_deadline;
    sock->tcp_keepalive_probes = keepalive_probes;
    return rc;
}

static void tcp_housekeeping(void) {
    uint64_t now = timer_ticks();

    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        net_socket_t* sock = &g_sockets[i];

        if (!sock->used || sock->kind != MYAOS_SOCK_PROTO_TCP || !sock->tcp_external) {
            continue;
        }

        if (sock->state != NET_TCP_STATE_ESTABLISHED && sock->state != NET_TCP_STATE_SYN_RECV &&
            sock->state != NET_TCP_STATE_SYN_SENT) {
            continue;
        }

        if (sock->tcp_unacked_active && now >= sock->tcp_unacked_deadline) {
            if (sock->tcp_retransmits >= NET_TCP_MAX_RETRANS) {
                close_socket_slot(i);
                continue;
            }
            if (tcp_retransmit_unacked(sock) != 0) {
                if (sock->tcp_retransmits < 0xFFu) {
                    sock->tcp_retransmits++;
                }
                sock->tcp_unacked_deadline = now + NET_TCP_RTO_TICKS;
                if (sock->tcp_retransmits >= NET_TCP_MAX_RETRANS) {
                    close_socket_slot(i);
                    continue;
                }
            }
        }

        if (sock->state != NET_TCP_STATE_ESTABLISHED) {
            continue;
        }

        if (!sock->tcp_unacked_active && sock->tcp_nagle_len > 0u && sock->tcp_nagle_deadline != 0u &&
            now >= sock->tcp_nagle_deadline) {
            (void)tcp_try_flush_nagle(sock);
        }

        if (sock->tcp_keepalive_deadline != 0u && now >= sock->tcp_keepalive_deadline) {
            if (sock->tcp_keepalive_probes >= NET_TCP_KEEPALIVE_MAX_PROBES || tcp_send_keepalive(sock) != 0) {
                close_socket_slot(i);
                continue;
            }
            sock->tcp_keepalive_probes++;
            sock->tcp_keepalive_deadline = now + NET_TCP_KEEPALIVE_INTERVAL_TICKS;
        }
    }
}

static void handle_arp_frame(const uint8_t* payload, uint16_t len) {
    uint16_t htype;
    uint16_t ptype;
    uint16_t opcode;
    uint32_t sender_ip;
    uint32_t target_ip;
    const uint8_t* sender_mac;
    const uint8_t* target_mac;

    if (!payload || len < NET_ARP_PKT_SIZE) {
        return;
    }

    htype = read_be16(&payload[0]);
    ptype = read_be16(&payload[2]);
    if (htype != 1u || ptype != NET_ETH_TYPE_IPV4 || payload[4] != 6u || payload[5] != 4u) {
        return;
    }

    opcode = read_be16(&payload[6]);
    sender_mac = &payload[8];
    sender_ip = read_be32(&payload[14]);
    target_mac = &payload[18];
    target_ip = read_be32(&payload[24]);

    (void)target_mac;
    arp_learn(sender_ip, sender_mac);

    if (opcode == 1u) {
        if (g_if.src_ip != 0u && target_ip == g_if.src_ip) {
            (void)arp_send_reply(sender_mac, sender_ip);
        }
    }
}

static int dhcp_option_u32(const uint8_t* opts, uint32_t len, uint8_t code, uint32_t* out) {
    uint32_t i = 0u;

    if (!opts || !out) {
        return -1;
    }

    while (i < len) {
        uint8_t c = opts[i++];
        if (c == 0u) {
            continue;
        }
        if (c == 255u) {
            break;
        }
        if (i >= len) {
            break;
        }

        {
            uint8_t l = opts[i++];
            if (i + l > len) {
                break;
            }
            if (c == code && l >= 4u) {
                *out = read_be32(&opts[i]);
                return 0;
            }
            i += l;
        }
    }

    return -1;
}

static int dhcp_option_u8(const uint8_t* opts, uint32_t len, uint8_t code, uint8_t* out) {
    uint32_t i = 0u;

    if (!opts || !out) {
        return -1;
    }

    while (i < len) {
        uint8_t c = opts[i++];
        if (c == 0u) {
            continue;
        }
        if (c == 255u) {
            break;
        }
        if (i >= len) {
            break;
        }

        {
            uint8_t l = opts[i++];
            if (i + l > len) {
                break;
            }
            if (c == code && l >= 1u) {
                *out = opts[i];
                return 0;
            }
            i += l;
        }
    }

    return -1;
}

static int parse_dhcp_payload(
    const uint8_t* udp_payload,
    uint16_t udp_payload_len,
    uint32_t xid,
    uint8_t wanted_msg,
    net_dhcp_offer_t* out_offer
) {
    uint8_t msg_type = 0u;

    if (!udp_payload || udp_payload_len < 240u || !out_offer) {
        return -1;
    }
    if (udp_payload[0] != 2u || udp_payload[1] != 1u || udp_payload[2] != 6u) {
        return -1;
    }
    if (read_be32(&udp_payload[4]) != xid) {
        return -1;
    }
    if (!mem_eq(&udp_payload[28], g_if.mac, 6u)) {
        return -1;
    }
    if (read_be32(&udp_payload[236]) != NET_DHCP_MAGIC_COOKIE) {
        return -1;
    }

    if (dhcp_option_u8(&udp_payload[240], (uint32_t)(udp_payload_len - 240u), 53u, &msg_type) != 0) {
        return -1;
    }
    if (msg_type != wanted_msg) {
        return -1;
    }

    out_offer->yiaddr = read_be32(&udp_payload[16]);
    out_offer->server_id = 0u;
    out_offer->subnet_mask = NET_DEFAULT_SUBNET_MASK;
    out_offer->router = NET_DEFAULT_GATEWAY_IP;
    out_offer->dns = NET_DEFAULT_GATEWAY_IP;
    (void)dhcp_option_u32(&udp_payload[240], (uint32_t)(udp_payload_len - 240u), 54u, &out_offer->server_id);
    (void)dhcp_option_u32(&udp_payload[240], (uint32_t)(udp_payload_len - 240u), 1u, &out_offer->subnet_mask);
    (void)dhcp_option_u32(&udp_payload[240], (uint32_t)(udp_payload_len - 240u), 3u, &out_offer->router);
    (void)dhcp_option_u32(&udp_payload[240], (uint32_t)(udp_payload_len - 240u), 6u, &out_offer->dns);
    return 0;
}

static int dhcp_try_capture_from_frame(
    const uint8_t* frame,
    uint16_t frame_len,
    uint32_t xid,
    uint8_t wanted_msg,
    net_dhcp_offer_t* out_offer
) {
    const uint8_t* ip;
    const uint8_t* udp;
    uint16_t eth_type;
    uint8_t ihl;
    uint16_t udp_len;

    if (!frame || frame_len < NET_ETH_HDR_SIZE + NET_IP_HDR_SIZE + NET_UDP_HDR_SIZE) {
        return -1;
    }

    eth_type = read_be16(&frame[12]);
    if (eth_type != NET_ETH_TYPE_IPV4) {
        return -1;
    }

    ip = &frame[NET_ETH_HDR_SIZE];
    if ((ip[0] >> 4) != 4u) {
        return -1;
    }
    ihl = (uint8_t)((ip[0] & 0x0Fu) * 4u);
    if (ihl < NET_IP_HDR_SIZE || frame_len < NET_ETH_HDR_SIZE + ihl + NET_UDP_HDR_SIZE) {
        return -1;
    }
    if (ip[9] != NET_IP_PROTO_UDP) {
        return -1;
    }

    udp = &ip[ihl];
    if (read_be16(&udp[2]) != NET_DHCP_CLIENT_PORT || read_be16(&udp[0]) != NET_DHCP_SERVER_PORT) {
        return -1;
    }

    udp_len = read_be16(&udp[4]);
    if (udp_len < NET_UDP_HDR_SIZE || NET_ETH_HDR_SIZE + ihl + udp_len > frame_len) {
        return -1;
    }

    return parse_dhcp_payload(&udp[NET_UDP_HDR_SIZE], (uint16_t)(udp_len - NET_UDP_HDR_SIZE), xid, wanted_msg, out_offer);
}

static int dhcp_send_message(uint8_t msg_type, uint32_t xid, uint32_t req_ip, uint32_t server_id) {
    uint8_t payload[300];
    uint32_t pos = 0u;
    static const uint8_t bcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    mem_zero(payload, sizeof(payload));

    payload[0] = 1u;
    payload[1] = 1u;
    payload[2] = 6u;
    payload[3] = 0u;
    write_be32(&payload[4], xid);
    write_be16(&payload[8], 0u);
    write_be16(&payload[10], 0x8000u);
    mem_copy(&payload[28], g_if.mac, 6u);
    write_be32(&payload[236], NET_DHCP_MAGIC_COOKIE);

    pos = 240u;
    payload[pos++] = 53u;
    payload[pos++] = 1u;
    payload[pos++] = msg_type;

    payload[pos++] = 55u;
    payload[pos++] = 3u;
    payload[pos++] = 1u;
    payload[pos++] = 3u;
    payload[pos++] = 6u;

    if (msg_type == NET_DHCP_MSG_REQUEST && req_ip != 0u && server_id != 0u) {
        payload[pos++] = 50u;
        payload[pos++] = 4u;
        write_be32(&payload[pos], req_ip);
        pos += 4u;

        payload[pos++] = 54u;
        payload[pos++] = 4u;
        write_be32(&payload[pos], server_id);
        pos += 4u;
    }

    payload[pos++] = 255u;

    return send_udp_ipv4(0u, 0xFFFFFFFFu, NET_DHCP_CLIENT_PORT, NET_DHCP_SERVER_PORT, payload, (uint16_t)pos, bcast);
}

static int dhcp_wait_message(uint32_t xid, uint8_t wanted_msg, net_dhcp_offer_t* out_offer) {
    for (uint32_t i = 0; i < NET_DHCP_WAIT_POLLS; i++) {
        uint8_t frame[NET_ETH_MAX_FRAME];
        uint16_t frame_len = 0u;

        if (netnic_poll_frame(frame, sizeof(frame), &frame_len) == 0) {
            if (dhcp_try_capture_from_frame(frame, frame_len, xid, wanted_msg, out_offer) == 0) {
                return 0;
            }
            if (frame_len >= NET_ETH_HDR_SIZE) {
                g_stats.rx_packets++;
                net_handle_frame(frame, frame_len);
            }
        }
        cpu_relax();
    }

    return -1;
}

static int dhcp_acquire_address(void) {
    net_dhcp_offer_t offer;
    net_dhcp_offer_t ack;
    uint32_t xid;

    xid = 0x4D594100u ^ ((uint32_t)g_if.mac[2] << 16) ^ ((uint32_t)g_if.mac[3] << 8) ^ g_if.mac[4];
    xid ^= (uint32_t)g_if.mac[5] << 24;

    if (dhcp_send_message(NET_DHCP_MSG_DISCOVER, xid, 0u, 0u) != 0) {
        return -1;
    }
    if (dhcp_wait_message(xid, NET_DHCP_MSG_OFFER, &offer) != 0) {
        return -1;
    }
    if (offer.yiaddr == 0u || offer.server_id == 0u) {
        return -1;
    }

    if (dhcp_send_message(NET_DHCP_MSG_REQUEST, xid, offer.yiaddr, offer.server_id) != 0) {
        return -1;
    }
    if (dhcp_wait_message(xid, NET_DHCP_MSG_ACK, &ack) != 0) {
        return -1;
    }

    if (ack.yiaddr == 0u) {
        return -1;
    }

    g_if.src_ip = ack.yiaddr;
    g_if.subnet_mask = (ack.subnet_mask != 0u) ? ack.subnet_mask : NET_DEFAULT_SUBNET_MASK;
    g_if.gateway_ip = (ack.router != 0u) ? ack.router : offer.router;
    if (g_if.gateway_ip == 0u) {
        g_if.gateway_ip = NET_DEFAULT_GATEWAY_IP;
    }
    g_if.dns_ip = (ack.dns != 0u) ? ack.dns : g_if.gateway_ip;
    if (g_if.dns_ip == 0u) {
        g_if.dns_ip = NET_DEFAULT_GATEWAY_IP;
    }
    return 0;
}

static int handle_udp_ipv4(uint32_t src_ip, uint32_t dst_ip, const uint8_t* udp, uint16_t udp_len) {
    uint16_t src_port;
    uint16_t dst_port;
    const uint8_t* payload;
    uint16_t payload_len;

    (void)dst_ip;

    if (!udp || udp_len < NET_UDP_HDR_SIZE) {
        return -1;
    }

    src_port = read_be16(&udp[0]);
    dst_port = read_be16(&udp[2]);
    payload = &udp[NET_UDP_HDR_SIZE];
    payload_len = (uint16_t)(udp_len - NET_UDP_HDR_SIZE);

    if (dst_port == NET_DHCP_CLIENT_PORT) {
        return 0;
    }

    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        net_socket_t* dst = &g_sockets[i];
        net_udp_dgram_t* dgram;
        uint32_t copy_len;
        uint32_t queue_pos;

        if (!dst->used || dst->kind != MYAOS_SOCK_PROTO_UDP || dst->local_port != dst_port) {
            continue;
        }
        if (dst->udp_len >= NET_UDP_QUEUE_DEPTH) {
            g_stats.dropped_packets++;
            return -1;
        }

        copy_len = min_u32(payload_len, NET_UDP_MAX_PAYLOAD);
        queue_pos = (dst->udp_head + dst->udp_len) % NET_UDP_QUEUE_DEPTH;
        dgram = &dst->udp_queue[queue_pos];
        dgram->src_port = src_port;
        dgram->len = (uint16_t)copy_len;
        if (copy_len > 0u) {
            mem_copy(dgram->payload, payload, copy_len);
        }
        dst->udp_len++;
        (void)src_ip;
        return 0;
    }

    return -1;
}

static void handle_icmp_ipv4(uint32_t src_ip, uint32_t dst_ip, const uint8_t* icmp, uint16_t icmp_len) {
    uint8_t reply[NET_IP_MAX_PAYLOAD];
    uint16_t ident;
    uint16_t seq;

    if (!icmp || icmp_len < 8u) {
        return;
    }
    if (dst_ip != g_if.src_ip || icmp[1] != 0u) {
        return;
    }

    if (icmp[0] == 0u) {
        ident = read_be16(&icmp[4]);
        seq = read_be16(&icmp[6]);
        if (g_ping_wait.active && !g_ping_wait.matched && g_ping_wait.src_ip == src_ip && g_ping_wait.ident == ident &&
            g_ping_wait.seq == seq) {
            g_ping_wait.matched = 1u;
            g_ping_wait.end_tick = timer_ticks();
        }
        return;
    }

    if (icmp[0] != 8u) {
        return;
    }

    if (icmp_len > sizeof(reply)) {
        return;
    }

    mem_copy(reply, icmp, icmp_len);
    reply[0] = 0u;
    reply[1] = 0u;
    write_be16(&reply[2], 0u);
    write_be16(&reply[2], ip_checksum16(reply, icmp_len));
    (void)send_ipv4_packet(g_if.src_ip, src_ip, NET_IP_PROTO_ICMP, reply, icmp_len, NULL);
}

static void handle_tcp_ipv4(uint32_t src_ip, uint32_t dst_ip, const uint8_t* tcp, uint16_t tcp_len) {
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t hdr_len;
    uint8_t flags;
    uint32_t seq;
    uint32_t ack;
    const uint8_t* payload;
    uint16_t payload_len;
    int slot;

    (void)dst_ip;

    if (!tcp || tcp_len < NET_TCP_HDR_SIZE) {
        return;
    }

    src_port = read_be16(&tcp[0]);
    dst_port = read_be16(&tcp[2]);
    seq = read_be32(&tcp[4]);
    ack = read_be32(&tcp[8]);
    hdr_len = (uint8_t)((tcp[12] >> 4) * 4u);
    if (hdr_len < NET_TCP_HDR_SIZE || hdr_len > tcp_len) {
        return;
    }
    flags = tcp[13];
    payload = &tcp[hdr_len];
    payload_len = (uint16_t)(tcp_len - hdr_len);

    slot = find_external_tcp_slot(dst_port, src_ip, src_port);
    if (slot >= 0) {
        net_socket_t* sock = &g_sockets[(uint32_t)slot];
        uint32_t peer_window = (uint32_t)read_be16(&tcp[14]);

        if (sock->tcp_peer_wscale > 14u) {
            sock->tcp_peer_wscale = 14u;
        }
        if (sock->tcp_peer_wscale > 0u) {
            peer_window <<= sock->tcp_peer_wscale;
        }
        sock->tcp_peer_window = peer_window;
        tcp_mark_activity(sock);

        if ((flags & 0x04u) != 0u) {
            close_socket_slot((uint32_t)slot);
            return;
        }

        if (sock->state == NET_TCP_STATE_SYN_SENT) {
            if ((flags & 0x12u) == 0x12u && ack == sock->tcp_tx_seq) {
                uint8_t peer_ws = 0u;
                if (tcp_parse_window_scale_opt(tcp, hdr_len, &peer_ws) == 0) {
                    sock->tcp_peer_wscale = peer_ws;
                }
                sock->tcp_rx_next = seq + 1u;
                sock->state = NET_TCP_STATE_ESTABLISHED;
                tcp_clear_unacked(sock);
                sock->tcp_last_ack = ack;
                (void)tcp_send_segment(sock, 0x10u, NULL, 0u);
                (void)tcp_try_flush_nagle(sock);
            }
            return;
        }

        if (sock->state == NET_TCP_STATE_SYN_RECV) {
            if ((flags & 0x10u) != 0u && ack == sock->tcp_tx_seq) {
                net_socket_t* listener = (sock->listener_slot >= 0 && socket_slot_valid((uint32_t)sock->listener_slot))
                                             ? &g_sockets[(uint32_t)sock->listener_slot]
                                             : NULL;
                sock->state = NET_TCP_STATE_ESTABLISHED;
                tcp_clear_unacked(sock);
                sock->tcp_last_ack = ack;
                if (listener) {
                    if (accept_queue_push(listener, slot) != 0) {
                        close_socket_slot((uint32_t)slot);
                    }
                }
            }
            return;
        }

        if (sock->state != NET_TCP_STATE_ESTABLISHED) {
            return;
        }

        if ((flags & 0x10u) != 0u) {
            if (sock->tcp_unacked_active) {
                uint32_t unacked_end =
                    sock->tcp_unacked_seq + tcp_segment_consumed(sock->tcp_unacked_flags, sock->tcp_unacked_len);

                if (tcp_seq_ge(ack, unacked_end)) {
                    tcp_clear_unacked(sock);
                    sock->tcp_last_ack = ack;
                    (void)tcp_try_flush_nagle(sock);
                } else if (tcp_seq_ge(ack, sock->tcp_unacked_seq + 1u) && (sock->tcp_unacked_flags & 0x03u) == 0u &&
                           sock->tcp_unacked_len > 0u) {
                    uint32_t acked = ack - sock->tcp_unacked_seq;
                    if (acked >= sock->tcp_unacked_len) {
                        tcp_clear_unacked(sock);
                        sock->tcp_last_ack = ack;
                        (void)tcp_try_flush_nagle(sock);
                    } else {
                        mem_move(sock->tcp_unacked_data, &sock->tcp_unacked_data[acked], sock->tcp_unacked_len - acked);
                        sock->tcp_unacked_len = (uint16_t)(sock->tcp_unacked_len - acked);
                        sock->tcp_unacked_seq = ack;
                        sock->tcp_unacked_deadline = timer_ticks() + NET_TCP_RTO_TICKS;
                        sock->tcp_retransmits = 0u;
                        sock->tcp_last_ack = ack;
                        sock->tcp_dup_ack_count = 0u;
                    }
                } else if (ack == sock->tcp_last_ack && payload_len == 0u) {
                    if (sock->tcp_dup_ack_count < 0xFFu) {
                        sock->tcp_dup_ack_count++;
                    }
                    if (sock->tcp_dup_ack_count >= 3u) {
                        (void)tcp_retransmit_unacked(sock);
                        sock->tcp_dup_ack_count = 0u;
                    }
                } else {
                    sock->tcp_last_ack = ack;
                    sock->tcp_dup_ack_count = 0u;
                }
            } else {
                sock->tcp_last_ack = ack;
                sock->tcp_dup_ack_count = 0u;
            }
        }

        if (payload_len > 0u) {
            if (seq == sock->tcp_rx_next) {
                uint32_t copied = tcp_rx_write(sock, payload, payload_len);
                sock->tcp_rx_next += copied;
                if (copied < payload_len) {
                    g_stats.dropped_packets++;
                }
                if (sock->tcp_ooo_valid && sock->tcp_ooo_seq == sock->tcp_rx_next) {
                    uint32_t copied_ooo = tcp_rx_write(sock, sock->tcp_ooo_data, sock->tcp_ooo_len);
                    sock->tcp_rx_next += copied_ooo;
                    if (copied_ooo < sock->tcp_ooo_len) {
                        g_stats.dropped_packets++;
                    }
                    sock->tcp_ooo_valid = 0u;
                    sock->tcp_ooo_len = 0u;
                    sock->tcp_ooo_seq = 0u;
                }
                (void)tcp_send_segment(sock, 0x10u, NULL, 0u);
            } else if (tcp_seq_ge(seq, sock->tcp_rx_next + 1u)) {
                uint16_t save_len = min_u32(payload_len, NET_TCP_TX_MAX_PAYLOAD);
                if (!sock->tcp_ooo_valid || tcp_seq_lt(seq, sock->tcp_ooo_seq) || seq == sock->tcp_ooo_seq) {
                    if (save_len > 0u) {
                        mem_copy(sock->tcp_ooo_data, payload, save_len);
                    }
                    sock->tcp_ooo_valid = 1u;
                    sock->tcp_ooo_len = save_len;
                    sock->tcp_ooo_seq = seq;
                }
                (void)tcp_send_segment(sock, 0x10u, NULL, 0u);
            } else {
                (void)tcp_send_segment(sock, 0x10u, NULL, 0u);
            }
        }

        if ((flags & 0x01u) != 0u) {
            uint32_t fin_seq = seq + payload_len;
            if (fin_seq == sock->tcp_rx_next) {
                sock->tcp_rx_next++;
            }
            sock->peer_closed = 1u;
            (void)tcp_send_segment(sock, 0x10u, NULL, 0u);
        }

        return;
    }

    if ((flags & 0x02u) != 0u && (flags & 0x10u) == 0u) {
        int listener_slot = find_tcp_listener_slot(dst_port);
        int child_slot;
        net_socket_t* listener;
        net_socket_t* child;

        if (listener_slot < 0) {
            return;
        }
        listener = &g_sockets[(uint32_t)listener_slot];
        if (listener->accept_len >= listener->backlog) {
            g_stats.dropped_packets++;
            return;
        }
        if (create_socket_slot(listener->owner_pid, MYAOS_SOCK_PROTO_TCP, &child_slot) != 0) {
            return;
        }

        child = &g_sockets[(uint32_t)child_slot];
        child->state = NET_TCP_STATE_SYN_RECV;
        child->tcp_external = 1u;
        child->local_port = dst_port;
        child->peer_port = src_port;
        child->peer_ip = src_ip;
        child->peer_slot = -1;
        child->listener_slot = listener_slot;
        child->peer_closed = 0u;
        child->tcp_rx_next = seq + 1u;
        child->tcp_local_wscale = NET_TCP_DEFAULT_WS_SHIFT;
        child->tcp_peer_wscale = 0u;
        (void)tcp_parse_window_scale_opt(tcp, hdr_len, &child->tcp_peer_wscale);
        child->tcp_peer_window = (uint32_t)read_be16(&tcp[14]);
        if (child->tcp_peer_wscale > 0u) {
            child->tcp_peer_window <<= child->tcp_peer_wscale;
        }
        child->tcp_tx_seq = g_if.tcp_isn_seed;
        g_if.tcp_isn_seed += 4099u;
        tcp_mark_activity(child);

        if (tcp_send_segment(child, 0x12u, NULL, 0u) != 0) {
            close_socket_slot((uint32_t)child_slot);
        }
    }
}

static void handle_ipv4_frame(const uint8_t* ip, uint16_t len) {
    uint8_t ihl;
    uint16_t total_len;
    uint8_t proto;
    uint32_t src_ip;
    uint32_t dst_ip;
    const uint8_t* payload;
    uint16_t payload_len;

    if (!ip || len < NET_IP_HDR_SIZE) {
        return;
    }
    if ((ip[0] >> 4) != 4u) {
        return;
    }

    ihl = (uint8_t)((ip[0] & 0x0Fu) * 4u);
    if (ihl < NET_IP_HDR_SIZE || len < ihl) {
        return;
    }

    total_len = read_be16(&ip[2]);
    if (total_len < ihl || total_len > len) {
        return;
    }

    src_ip = read_be32(&ip[12]);
    dst_ip = read_be32(&ip[16]);
    proto = ip[9];

    if (dst_ip != g_if.src_ip && !ip_is_broadcast(dst_ip)) {
        return;
    }

    payload = &ip[ihl];
    payload_len = (uint16_t)(total_len - ihl);

    if (proto == NET_IP_PROTO_UDP) {
        (void)handle_udp_ipv4(src_ip, dst_ip, payload, payload_len);
    } else if (proto == NET_IP_PROTO_ICMP) {
        handle_icmp_ipv4(src_ip, dst_ip, payload, payload_len);
    } else if (proto == NET_IP_PROTO_TCP) {
        handle_tcp_ipv4(src_ip, dst_ip, payload, payload_len);
    }
}

static void net_handle_frame(const uint8_t* frame, uint16_t len) {
    uint16_t eth_type;

    if (!frame || len < NET_ETH_HDR_SIZE) {
        return;
    }

    eth_type = read_be16(&frame[12]);
    if (eth_type == NET_ETH_TYPE_ARP) {
        handle_arp_frame(&frame[NET_ETH_HDR_SIZE], (uint16_t)(len - NET_ETH_HDR_SIZE));
    } else if (eth_type == NET_ETH_TYPE_IPV4) {
        handle_ipv4_frame(&frame[NET_ETH_HDR_SIZE], (uint16_t)(len - NET_ETH_HDR_SIZE));
    }
}

static int net_configure_active_nic(void) {
    if (netnic_init() != 0 || netnic_get_mac(g_if.mac) != 0) {
        g_if.has_link = 0u;
        g_if.have_gateway_mac = 0u;
        return -1;
    }

    g_if.has_link = 1u;
    g_if.have_gateway_mac = 0u;
    g_if.src_ip = NET_DEFAULT_SRC_IP;
    g_if.gateway_ip = NET_DEFAULT_GATEWAY_IP;
    g_if.subnet_mask = NET_DEFAULT_SUBNET_MASK;
    g_if.dns_ip = NET_DEFAULT_GATEWAY_IP;
    arp_cache_reset();
    arp_pending_reset();

    if (dhcp_acquire_address() != 0) {
        g_if.src_ip = NET_DEFAULT_SRC_IP;
        g_if.gateway_ip = NET_DEFAULT_GATEWAY_IP;
        g_if.subnet_mask = NET_DEFAULT_SUBNET_MASK;
        g_if.dns_ip = NET_DEFAULT_GATEWAY_IP;
    }
    if (g_if.gateway_ip != 0u) {
        uint8_t gw_mac[6];
        if (arp_resolve(g_if.gateway_ip, gw_mac) == 0) {
            mem_copy(g_if.gateway_mac, gw_mac, 6u);
            g_if.have_gateway_mac = 1u;
        }
    }

    return 0;
}

void net_init(void) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        mem_zero(&g_sockets[i], sizeof(g_sockets[i]));
        g_sockets[i].peer_slot = -1;
        g_sockets[i].listener_slot = -1;
        accept_queue_reset(&g_sockets[i]);
    }
    mem_zero(&g_stats, sizeof(g_stats));
    mem_zero(&g_if, sizeof(g_if));
    mem_zero(&g_ping_wait, sizeof(g_ping_wait));
    g_if.src_ip = NET_DEFAULT_SRC_IP;
    g_if.gateway_ip = NET_DEFAULT_GATEWAY_IP;
    g_if.subnet_mask = NET_DEFAULT_SUBNET_MASK;
    g_if.dns_ip = NET_DEFAULT_GATEWAY_IP;
    g_if.ip_ident = 1u;
    g_if.tcp_isn_seed = 0x12340000u;
    g_udp4_src_port_next = NET_EPHEMERAL_PORT_MIN;
    arp_cache_reset();
    arp_pending_reset();
    g_net_initialized = 1u;

    (void)device_register(MYAOS_DEV_NETWORK, "lo0", "net.loop", NULL, &g_net_device_ops, NULL);
    (void)net_configure_active_nic();
}

int net_reprobe(void) {
    if (!g_net_initialized) {
        return -1;
    }
    return net_configure_active_nic();
}

int net_socket_open_ex(int32_t owner_pid, uint8_t proto, uint16_t local_port, int32_t* out_fd) {
    int32_t slot;
    net_socket_t* sock;

    net_poll_rx(16u);

    if (!out_fd || owner_pid <= 0) {
        return -1;
    }
    if (proto != MYAOS_SOCK_PROTO_UDP && proto != MYAOS_SOCK_PROTO_TCP) {
        return -1;
    }
    if (create_socket_slot(owner_pid, proto, &slot) != 0) {
        return -1;
    }

    sock = &g_sockets[(uint32_t)slot];

    if (proto == MYAOS_SOCK_PROTO_UDP) {
        if (local_port == 0u) {
            local_port = pick_udp_ephemeral_port();
        }
        if (local_port == 0u || udp_port_in_use(local_port, slot)) {
            close_socket_slot((uint32_t)slot);
            return -1;
        }
        sock->local_port = local_port;
    } else if (local_port != 0u) {
        if (tcp_port_reserved(local_port, slot)) {
            close_socket_slot((uint32_t)slot);
            return -1;
        }
        sock->local_port = local_port;
    }

    *out_fd = slot + 1;
    return 0;
}

int net_socket_open(int32_t owner_pid, uint16_t local_port, int32_t* out_fd) {
    return net_socket_open_ex(owner_pid, MYAOS_SOCK_PROTO_UDP, local_port, out_fd);
}

int net_socket_bind(int32_t owner_pid, int32_t fd, uint16_t local_port) {
    net_socket_t* sock;

    net_poll_rx(16u);

    sock = socket_for_owner(owner_pid, fd);

    if (!sock || local_port == 0u) {
        return -1;
    }

    if (sock->kind == MYAOS_SOCK_PROTO_UDP) {
        if (udp_port_in_use(local_port, fd - 1)) {
            return -1;
        }
        sock->local_port = local_port;
        return 0;
    }

    if (sock->kind != MYAOS_SOCK_PROTO_TCP || sock->state != NET_TCP_STATE_CLOSED || sock->peer_slot >= 0) {
        return -1;
    }
    if (tcp_port_reserved(local_port, fd - 1)) {
        return -1;
    }

    sock->local_port = local_port;
    return 0;
}

int net_socket_listen(int32_t owner_pid, int32_t fd, uint16_t backlog) {
    net_socket_t* sock;

    net_poll_rx(16u);

    sock = socket_for_owner(owner_pid, fd);

    if (!sock || sock->kind != MYAOS_SOCK_PROTO_TCP) {
        return -1;
    }
    if (sock->state != NET_TCP_STATE_CLOSED || sock->peer_slot >= 0 || sock->local_port == 0u) {
        return -1;
    }
    if (find_tcp_listener_slot(sock->local_port) >= 0 && find_tcp_listener_slot(sock->local_port) != (fd - 1)) {
        return -1;
    }

    if (backlog == 0u) {
        backlog = 1u;
    }
    if (backlog > NET_TCP_LISTEN_BACKLOG_MAX) {
        backlog = NET_TCP_LISTEN_BACKLOG_MAX;
    }

    sock->state = NET_TCP_STATE_LISTEN;
    sock->backlog = backlog;
    accept_queue_reset(sock);
    return 0;
}

int net_socket_connect(int32_t owner_pid, int32_t fd, uint16_t dst_port) {
    net_socket_t* client;
    int listener_slot;
    net_socket_t* listener;
    int child_slot;
    net_socket_t* child;

    net_poll_rx(16u);

    client = socket_for_owner(owner_pid, fd);

    if (!client || client->kind != MYAOS_SOCK_PROTO_TCP || dst_port == 0u) {
        return -1;
    }
    if (client->state != NET_TCP_STATE_CLOSED || client->peer_slot >= 0) {
        return -1;
    }

    listener_slot = find_tcp_listener_slot(dst_port);
    if (listener_slot < 0) {
        return -1;
    }
    listener = &g_sockets[(uint32_t)listener_slot];

    if (client->local_port == 0u) {
        client->local_port = pick_tcp_ephemeral_port();
    }
    if (client->local_port == 0u) {
        return -1;
    }
    if (tcp_port_reserved(client->local_port, fd - 1)) {
        return -1;
    }
    if (tcp_tuple_in_use(client->local_port, dst_port, fd - 1)) {
        return -1;
    }
    if (tcp_tuple_in_use(dst_port, client->local_port, -1)) {
        return -1;
    }
    if (listener->accept_len >= listener->backlog) {
        g_stats.dropped_packets++;
        return -1;
    }

    if (create_socket_slot(listener->owner_pid, MYAOS_SOCK_PROTO_TCP, &child_slot) != 0) {
        return -1;
    }

    child = &g_sockets[(uint32_t)child_slot];
    child->state = NET_TCP_STATE_ESTABLISHED;
    child->local_port = listener->local_port;
    child->peer_port = client->local_port;
    child->peer_slot = fd - 1;
    child->listener_slot = listener_slot;

    client->state = NET_TCP_STATE_ESTABLISHED;
    client->peer_port = listener->local_port;
    client->peer_slot = child_slot;
    client->peer_closed = 0u;

    if (accept_queue_push(listener, child_slot) != 0) {
        close_socket_slot((uint32_t)child_slot);
        client->state = NET_TCP_STATE_CLOSED;
        client->peer_port = 0u;
        client->peer_slot = -1;
        client->peer_closed = 0u;
        g_stats.dropped_packets++;
        return -1;
    }

    return 0;
}

int net_socket_connect4(int32_t owner_pid, int32_t fd, uint32_t dst_ip, uint16_t dst_port) {
    net_socket_t* client;
    uint16_t original_local_port;

    net_poll_rx(16u);

    client = socket_for_owner(owner_pid, fd);

    if (!client || client->kind != MYAOS_SOCK_PROTO_TCP || dst_port == 0u || dst_ip == 0u || ip_is_broadcast(dst_ip)) {
        return -1;
    }
    if (client->state != NET_TCP_STATE_CLOSED || client->peer_slot >= 0) {
        return -1;
    }
    if (!g_if.has_link || g_if.src_ip == 0u) {
        return -1;
    }

    original_local_port = client->local_port;
    if (client->local_port == 0u) {
        client->local_port = pick_tcp_ephemeral_port();
    }
    if (client->local_port == 0u) {
        return -1;
    }
    if (tcp_port_reserved(client->local_port, fd - 1)) {
        if (original_local_port == 0u) {
            client->local_port = 0u;
        }
        return -1;
    }
    if (find_external_tcp_slot(client->local_port, dst_ip, dst_port) >= 0) {
        if (original_local_port == 0u) {
            client->local_port = 0u;
        }
        return -1;
    }

    client->state = NET_TCP_STATE_SYN_SENT;
    client->tcp_external = 1u;
    client->peer_ip = dst_ip;
    client->peer_port = dst_port;
    client->peer_slot = -1;
    client->listener_slot = -1;
    client->peer_closed = 0u;
    client->tcp_local_wscale = NET_TCP_DEFAULT_WS_SHIFT;
    client->tcp_peer_wscale = 0u;
    client->tcp_peer_window = NET_TCP_TX_MAX_PAYLOAD;
    client->tcp_rx_head = 0u;
    client->tcp_rx_len = 0u;
    client->tcp_rx_next = 0u;
    client->tcp_ooo_valid = 0u;
    client->tcp_ooo_len = 0u;
    client->tcp_ooo_seq = 0u;
    client->tcp_nagle_len = 0u;
    client->tcp_nagle_deadline = 0u;
    client->tcp_tx_seq = g_if.tcp_isn_seed;
    g_if.tcp_isn_seed += 4099u;
    tcp_mark_activity(client);

    if (tcp_send_segment(client, 0x02u, NULL, 0u) != 0) {
        client->state = NET_TCP_STATE_CLOSED;
        client->tcp_external = 0u;
        client->peer_ip = 0u;
        client->peer_port = 0u;
        client->peer_slot = -1;
        client->listener_slot = -1;
        client->peer_closed = 0u;
        client->tcp_peer_window = 0u;
        client->tcp_nagle_len = 0u;
        client->tcp_nagle_deadline = 0u;
        client->tcp_ooo_valid = 0u;
        client->tcp_ooo_len = 0u;
        client->tcp_ooo_seq = 0u;
        tcp_clear_unacked(client);
        if (original_local_port == 0u) {
            client->local_port = 0u;
        }
        return -1;
    }

    for (uint32_t i = 0; i < NET_TCP_CONNECT_WAIT_POLLS; i++) {
        net_poll_rx(16u);

        if (!socket_slot_valid((uint32_t)(fd - 1))) {
            return -1;
        }

        if (client->state == NET_TCP_STATE_ESTABLISHED) {
            return 0;
        }

        cpu_relax();
    }

    client->state = NET_TCP_STATE_CLOSED;
    client->tcp_external = 0u;
    client->peer_ip = 0u;
    client->peer_port = 0u;
    client->peer_slot = -1;
    client->listener_slot = -1;
    client->peer_closed = 0u;
    client->tcp_rx_head = 0u;
    client->tcp_rx_len = 0u;
    client->tcp_rx_next = 0u;
    client->tcp_peer_window = 0u;
    client->tcp_nagle_len = 0u;
    client->tcp_nagle_deadline = 0u;
    client->tcp_ooo_valid = 0u;
    client->tcp_ooo_len = 0u;
    client->tcp_ooo_seq = 0u;
    tcp_clear_unacked(client);
    if (original_local_port == 0u) {
        client->local_port = 0u;
    }
    return -1;
}

int net_socket_accept(int32_t owner_pid, int32_t fd, int32_t* out_fd, uint16_t* out_peer_port) {
    net_socket_t* listener;
    int child_slot;
    net_socket_t* child;

    net_poll_rx(64u);

    listener = socket_for_owner(owner_pid, fd);

    if (!listener || !out_fd || listener->kind != MYAOS_SOCK_PROTO_TCP || listener->state != NET_TCP_STATE_LISTEN) {
        return -1;
    }

    child_slot = accept_queue_pop(listener);
    if (child_slot < 0 || !socket_slot_valid((uint32_t)child_slot)) {
        return -1;
    }

    child = &g_sockets[(uint32_t)child_slot];
    if (child->owner_pid != owner_pid || child->kind != MYAOS_SOCK_PROTO_TCP ||
        child->state != NET_TCP_STATE_ESTABLISHED) {
        close_socket_slot((uint32_t)child_slot);
        return -1;
    }

    if (out_peer_port) {
        *out_peer_port = child->peer_port;
    }
    *out_fd = child_slot + 1;
    return 0;
}

int net_socket_close(int32_t owner_pid, int32_t fd) {
    net_socket_t* sock;

    net_poll_rx(16u);

    sock = socket_for_owner(owner_pid, fd);

    if (!sock) {
        return -1;
    }

    if (sock->kind == MYAOS_SOCK_PROTO_TCP && sock->tcp_external && sock->state == NET_TCP_STATE_ESTABLISHED) {
        if (!sock->peer_closed) {
            (void)tcp_send_segment(sock, 0x11u, NULL, 0u);
        } else {
            (void)tcp_send_segment(sock, 0x10u, NULL, 0u);
        }
    }

    close_socket_slot((uint32_t)(fd - 1));
    return 0;
}

int net_socket_sendto(
    int32_t owner_pid,
    int32_t fd,
    uint16_t dst_port,
    const void* data,
    uint32_t len,
    uint32_t* out_written
) {
    net_socket_t* src;
    const uint8_t* in = (const uint8_t*)data;

    net_poll_rx(64u);

    src = socket_for_owner(owner_pid, fd);

    if (!src || !out_written || (len != 0u && !data)) {
        return -1;
    }

    if (src->kind == MYAOS_SOCK_PROTO_UDP) {
        net_socket_t* dst;
        net_udp_dgram_t* dgram;
        uint32_t copy_len;
        uint32_t queue_pos;

        if (dst_port == 0u) {
            return -1;
        }
        if (src->local_port == 0u) {
            return -1;
        }

        for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
            if (!g_sockets[i].used || g_sockets[i].kind != MYAOS_SOCK_PROTO_UDP) {
                continue;
            }
            if (g_sockets[i].local_port == dst_port) {
                dst = &g_sockets[i];
                if (dst->udp_len >= NET_UDP_QUEUE_DEPTH) {
                    g_stats.dropped_packets++;
                    *out_written = 0u;
                    return -1;
                }

                copy_len = min_u32(len, NET_UDP_MAX_PAYLOAD);
                queue_pos = (dst->udp_head + dst->udp_len) % NET_UDP_QUEUE_DEPTH;
                dgram = &dst->udp_queue[queue_pos];
                dgram->src_port = src->local_port;
                dgram->len = (uint16_t)copy_len;
                if (copy_len > 0u) {
                    mem_copy(dgram->payload, in, copy_len);
                }
                dst->udp_len++;
                g_stats.tx_packets++;
                g_stats.rx_packets++;
                *out_written = copy_len;
                return 0;
            }
        }

        g_stats.dropped_packets++;
        *out_written = 0u;
        return -1;
    }

    if (src->kind != MYAOS_SOCK_PROTO_TCP || src->state != NET_TCP_STATE_ESTABLISHED) {
        return -1;
    }

    if (dst_port != 0u && dst_port != src->peer_port) {
        return -1;
    }

    if (src->peer_slot >= 0 && socket_slot_valid((uint32_t)src->peer_slot)) {
        net_socket_t* dst = &g_sockets[(uint32_t)src->peer_slot];
        uint32_t copy_len;

        if (dst->kind != MYAOS_SOCK_PROTO_TCP || dst->state != NET_TCP_STATE_ESTABLISHED) {
            *out_written = 0u;
            return -1;
        }

        copy_len = tcp_rx_write(dst, in, len);
        *out_written = copy_len;
        if (copy_len == 0u && len != 0u) {
            g_stats.dropped_packets++;
            return -1;
        }
        if (copy_len > 0u || len == 0u) {
            g_stats.tx_packets++;
            g_stats.rx_packets++;
        }
        return 0;
    }

    if (src->tcp_external && src->peer_ip != 0u) {
        uint16_t chunk;
        uint32_t queued = 0u;

        if (src->peer_closed) {
            *out_written = 0u;
            return -1;
        }

        if (len == 0u) {
            if (!src->tcp_unacked_active && src->tcp_nagle_len > 0u) {
                (void)tcp_try_flush_nagle(src);
            }
            if (tcp_send_segment(src, 0x10u, NULL, 0u) != 0) {
                *out_written = 0u;
                return -1;
            }
            *out_written = 0u;
            return 0;
        }

        if (src->tcp_unacked_active || src->tcp_peer_window == 0u) {
            queued = tcp_queue_nagle(src, in, len);
            *out_written = queued;
            return (queued > 0u) ? 0 : -1;
        }

        if (src->tcp_nagle_len > 0u) {
            queued = tcp_queue_nagle(src, in, len);
            if (src->tcp_nagle_len >= NET_TCP_TX_MAX_PAYLOAD || timer_ticks() >= src->tcp_nagle_deadline) {
                if (tcp_try_flush_nagle(src) != 0) {
                    *out_written = queued;
                    return (queued > 0u) ? 0 : -1;
                }
            }
            *out_written = queued;
            return 0;
        }

        if (len < NET_TCP_TX_MAX_PAYLOAD) {
            queued = tcp_queue_nagle(src, in, len);
            *out_written = queued;
            return (queued > 0u) ? 0 : -1;
        }

        chunk = (uint16_t)min_u32(len, NET_TCP_TX_MAX_PAYLOAD);
        if (src->tcp_peer_window > 0u && chunk > src->tcp_peer_window) {
            chunk = (uint16_t)src->tcp_peer_window;
        }
        if (chunk == 0u) {
            queued = tcp_queue_nagle(src, in, len);
            *out_written = queued;
            return (queued > 0u) ? 0 : -1;
        }

        if (tcp_send_segment(src, 0x18u, in, chunk) != 0) {
            g_stats.dropped_packets++;
            *out_written = 0u;
            return -1;
        }

        *out_written = chunk;
        return 0;
    }

    *out_written = 0u;
    return -1;
}

int net_socket_send(
    int32_t owner_pid,
    int32_t fd,
    uint16_t dst_port,
    const void* data,
    uint32_t len,
    uint32_t* out_written
) {
    return net_socket_sendto(owner_pid, fd, dst_port, data, len, out_written);
}

int net_socket_recvfrom(
    int32_t owner_pid,
    int32_t fd,
    void* out_buf,
    uint32_t max_len,
    uint32_t* out_len,
    uint16_t* out_src_port
) {
    net_socket_t* sock;
    uint8_t* out = (uint8_t*)out_buf;

    net_poll_rx(64u);

    sock = socket_for_owner(owner_pid, fd);

    if (!sock || !out_len || (max_len != 0u && !out_buf)) {
        return -1;
    }

    if (sock->kind == MYAOS_SOCK_PROTO_UDP) {
        net_udp_dgram_t* dgram;
        uint32_t copy_len;

        if (sock->udp_len == 0u) {
            return -1;
        }

        dgram = &sock->udp_queue[sock->udp_head];
        copy_len = min_u32((uint32_t)dgram->len, max_len);
        if (copy_len > 0u) {
            mem_copy(out, dgram->payload, copy_len);
        }
        if (out_src_port) {
            *out_src_port = dgram->src_port;
        }
        *out_len = copy_len;

        sock->udp_head = (sock->udp_head + 1u) % NET_UDP_QUEUE_DEPTH;
        sock->udp_len--;
        return 0;
    }

    if (sock->kind != MYAOS_SOCK_PROTO_TCP || sock->state != NET_TCP_STATE_ESTABLISHED) {
        return -1;
    }

    if (sock->tcp_rx_len == 0u) {
        if (sock->peer_closed) {
            *out_len = 0u;
            if (out_src_port) {
                *out_src_port = sock->peer_port;
            }
            return 0;
        }
        return -1;
    }

    *out_len = tcp_rx_read(sock, out, max_len);
    if (out_src_port) {
        *out_src_port = sock->peer_port;
    }
    return 0;
}

int net_socket_recv(
    int32_t owner_pid,
    int32_t fd,
    void* out_buf,
    uint32_t max_len,
    uint32_t* out_len,
    uint16_t* out_src_port
) {
    return net_socket_recvfrom(owner_pid, fd, out_buf, max_len, out_len, out_src_port);
}

int net_send_udp4(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const void* data, uint32_t len) {
    const uint8_t* in = (const uint8_t*)data;

    net_poll_rx(16u);

    if ((len != 0u && !data) || len > NET_UDP4_MAX_PAYLOAD || dst_port == 0u || dst_ip == 0u) {
        return -1;
    }
    if (!netnic_ready() || !g_if.has_link) {
        return -1;
    }

    if (src_port == 0u) {
        src_port = g_udp4_src_port_next++;
        if (g_udp4_src_port_next < NET_EPHEMERAL_PORT_MIN || g_udp4_src_port_next > NET_EPHEMERAL_PORT_MAX) {
            g_udp4_src_port_next = NET_EPHEMERAL_PORT_MIN;
        }
    }

    if (send_udp_ipv4(g_if.src_ip, dst_ip, src_port, dst_port, in, (uint16_t)len, NULL) != 0) {
        g_stats.dropped_packets++;
        return -1;
    }

    return (int)len;
}

int net_ping4(uint32_t dst_ip, uint16_t ident, uint16_t seq, uint32_t timeout_polls, uint32_t* out_rtt_ticks) {
    uint8_t icmp[16];
    uint64_t start;

    net_poll_rx(16u);

    if (out_rtt_ticks) {
        *out_rtt_ticks = 0u;
    }
    if (!netnic_ready() || !g_if.has_link || g_if.src_ip == 0u || dst_ip == 0u || ip_is_broadcast(dst_ip)) {
        return -1;
    }
    if (timeout_polls == 0u) {
        timeout_polls = 12000u;
    }

    mem_zero(icmp, sizeof(icmp));
    icmp[0] = 8u;
    icmp[1] = 0u;
    write_be16(&icmp[4], ident);
    write_be16(&icmp[6], seq);
    icmp[8] = 'M';
    icmp[9] = 'Y';
    icmp[10] = 'A';
    icmp[11] = 'O';
    icmp[12] = 'S';
    icmp[13] = 'P';
    icmp[14] = 'I';
    icmp[15] = 'N';
    write_be16(&icmp[2], 0u);
    write_be16(&icmp[2], ip_checksum16(icmp, sizeof(icmp)));

    g_ping_wait.active = 1u;
    g_ping_wait.matched = 0u;
    g_ping_wait.ident = ident;
    g_ping_wait.seq = seq;
    g_ping_wait.src_ip = dst_ip;
    start = timer_ticks();
    g_ping_wait.start_tick = start;
    g_ping_wait.end_tick = start;

    if (send_ipv4_packet(g_if.src_ip, dst_ip, NET_IP_PROTO_ICMP, icmp, sizeof(icmp), NULL) != 0) {
        g_ping_wait.active = 0u;
        return -1;
    }

    for (uint32_t i = 0; i < timeout_polls; i++) {
        net_poll_rx(16u);
        if (g_ping_wait.matched) {
            if (out_rtt_ticks) {
                uint64_t delta = g_ping_wait.end_tick - g_ping_wait.start_tick;
                if (delta > 0xFFFFFFFFu) {
                    delta = 0xFFFFFFFFu;
                }
                *out_rtt_ticks = (uint32_t)delta;
            }
            g_ping_wait.active = 0u;
            return 0;
        }
        cpu_relax();
    }

    g_ping_wait.active = 0u;
    return -1;
}

int net_info(myaos_netinfo_t* out) {
    uint32_t socket_count = 0u;
    uint32_t bound_ports = 0u;

    net_poll_rx(16u);

    if (!out) {
        return -1;
    }

    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].used) {
            continue;
        }

        socket_count++;
        if (g_sockets[i].local_port != 0u) {
            uint8_t seen = 0u;

            for (uint32_t j = 0; j < i; j++) {
                if (!g_sockets[j].used) {
                    continue;
                }
                if (g_sockets[j].local_port == g_sockets[i].local_port) {
                    seen = 1u;
                    break;
                }
            }
            if (!seen) {
                bound_ports++;
            }
        }
    }

    out->tx_packets = g_stats.tx_packets;
    out->rx_packets = g_stats.rx_packets;
    out->dropped_packets = g_stats.dropped_packets;
    out->socket_count = socket_count;
    out->bound_ports = bound_ports;
    return 0;
}

uint32_t net_default_src_ip(void) {
    return g_if.src_ip;
}

uint32_t net_default_gateway_ip(void) {
    return g_if.gateway_ip;
}

uint32_t net_default_dns_ip(void) {
    return g_if.dns_ip;
}

const char* net_if_name(void) {
    return netnic_if_name();
}

const char* net_driver_name(void) {
    return netnic_driver_name();
}

const char* net_nic_model(void) {
    return netnic_model_name();
}

const char* net_nic_state(void) {
    return netnic_state();
}

const char* net_nic_note(void) {
    return netnic_note();
}

const char* net_nic_firmware_state(void) {
    return netnic_firmware_state();
}

const char* net_nic_firmware_path(void) {
    return netnic_firmware_path();
}

const char* net_nic_pci_bdf(void) {
    return netnic_pci_bdf();
}

const char* net_nic_pci_id(void) {
    return netnic_pci_id();
}

uint32_t net_nic_detected_count(void) {
    return netnic_detected_count();
}

uint32_t net_nic_supported_count(void) {
    return netnic_supported_count();
}

void net_on_process_exit(int32_t pid) {
    if (pid <= 0) {
        return;
    }

    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (g_sockets[i].used && g_sockets[i].owner_pid == pid) {
            close_socket_slot(i);
        }
    }
}
