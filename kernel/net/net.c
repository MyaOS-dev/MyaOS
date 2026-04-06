#include "net.h"
#include "device.h"
#include "e1000.h"
#include <stddef.h>

#define NET_MAX_SOCKETS 32u
#define NET_UDP_QUEUE_DEPTH 8u
#define NET_UDP_MAX_PAYLOAD 512u
#define NET_TCP_RX_CAPACITY 4096u
#define NET_TCP_LISTEN_BACKLOG_MAX 16u
#define NET_EPHEMERAL_PORT_MIN 49152u
#define NET_EPHEMERAL_PORT_MAX 65535u
#define NET_ETH_TYPE_IPV4 0x0800u
#define NET_IP_PROTO_UDP 17u
#define NET_IP_HDR_SIZE 20u
#define NET_UDP_HDR_SIZE 8u
#define NET_ETH_HDR_SIZE 14u
#define NET_UDP4_MAX_PAYLOAD 1400u
#define NET_DEFAULT_SRC_IP 0x0A00020Fu

static const uint8_t g_default_gateway_mac[6] = {0x52u, 0x55u, 0x0Au, 0x00u, 0x02u, 0x02u};

typedef enum {
    NET_TCP_STATE_CLOSED = 0,
    NET_TCP_STATE_LISTEN = 1,
    NET_TCP_STATE_ESTABLISHED = 2,
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
    int32_t owner_pid;
    uint16_t local_port;
    uint16_t peer_port;
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
} net_socket_t;

typedef struct {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t dropped_packets;
} net_stats_t;

static net_socket_t g_sockets[NET_MAX_SOCKETS];
static net_stats_t g_stats;
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

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
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

        if (!it->used || it->kind != MYAOS_SOCK_PROTO_TCP || it->state != NET_TCP_STATE_ESTABLISHED) {
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
        } else if (sock->state == NET_TCP_STATE_ESTABLISHED) {
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

void net_init(void) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        mem_zero(&g_sockets[i], sizeof(g_sockets[i]));
        g_sockets[i].peer_slot = -1;
        g_sockets[i].listener_slot = -1;
        accept_queue_reset(&g_sockets[i]);
    }
    mem_zero(&g_stats, sizeof(g_stats));
    g_udp4_src_port_next = NET_EPHEMERAL_PORT_MIN;

    (void)device_register(MYAOS_DEV_NETWORK, "lo0", "net.loop", NULL, &g_net_device_ops, NULL);
    (void)e1000_init();
}

int net_socket_open_ex(int32_t owner_pid, uint8_t proto, uint16_t local_port, int32_t* out_fd) {
    int32_t slot;
    net_socket_t* sock;

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
    net_socket_t* sock = socket_for_owner(owner_pid, fd);

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
    net_socket_t* sock = socket_for_owner(owner_pid, fd);

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
    net_socket_t* client = socket_for_owner(owner_pid, fd);
    int listener_slot;
    net_socket_t* listener;
    int child_slot;
    net_socket_t* child;

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

int net_socket_accept(int32_t owner_pid, int32_t fd, int32_t* out_fd, uint16_t* out_peer_port) {
    net_socket_t* listener = socket_for_owner(owner_pid, fd);
    int child_slot;
    net_socket_t* child;

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
    net_socket_t* sock = socket_for_owner(owner_pid, fd);

    if (!sock) {
        return -1;
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
    net_socket_t* src = socket_for_owner(owner_pid, fd);
    const uint8_t* in = (const uint8_t*)data;

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
    if (src->peer_slot < 0 || !socket_slot_valid((uint32_t)src->peer_slot)) {
        *out_written = 0u;
        return -1;
    }

    {
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
    net_socket_t* sock = socket_for_owner(owner_pid, fd);
    uint8_t* out = (uint8_t*)out_buf;

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
    uint8_t frame[NET_ETH_HDR_SIZE + NET_IP_HDR_SIZE + NET_UDP_HDR_SIZE + NET_UDP4_MAX_PAYLOAD];
    uint8_t src_mac[6];
    uint8_t* ip;
    uint8_t* udp;
    uint32_t frame_len;
    uint16_t ip_total_len;
    uint16_t udp_len;

    if ((len != 0u && !data) || len > NET_UDP4_MAX_PAYLOAD || dst_port == 0u || dst_ip == 0u) {
        return -1;
    }
    if (!e1000_ready() || e1000_get_mac(src_mac) != 0) {
        return -1;
    }

    if (src_port == 0u) {
        src_port = g_udp4_src_port_next++;
        if (g_udp4_src_port_next < NET_EPHEMERAL_PORT_MIN || g_udp4_src_port_next > NET_EPHEMERAL_PORT_MAX) {
            g_udp4_src_port_next = NET_EPHEMERAL_PORT_MIN;
        }
    }

    for (uint32_t i = 0; i < 6u; i++) {
        frame[i] = g_default_gateway_mac[i];
        frame[6u + i] = src_mac[i];
    }
    frame[12] = (uint8_t)(NET_ETH_TYPE_IPV4 >> 8);
    frame[13] = (uint8_t)(NET_ETH_TYPE_IPV4 & 0xFFu);

    ip = &frame[NET_ETH_HDR_SIZE];
    udp = &frame[NET_ETH_HDR_SIZE + NET_IP_HDR_SIZE];
    ip_total_len = (uint16_t)(NET_IP_HDR_SIZE + NET_UDP_HDR_SIZE + len);
    udp_len = (uint16_t)(NET_UDP_HDR_SIZE + len);

    ip[0] = 0x45u;
    ip[1] = 0u;
    write_be16(&ip[2], ip_total_len);
    write_be16(&ip[4], 0u);
    write_be16(&ip[6], 0u);
    ip[8] = 64u;
    ip[9] = NET_IP_PROTO_UDP;
    write_be16(&ip[10], 0u);
    write_be32(&ip[12], NET_DEFAULT_SRC_IP);
    write_be32(&ip[16], dst_ip);
    write_be16(&ip[10], ip_checksum16(ip, NET_IP_HDR_SIZE));

    write_be16(&udp[0], src_port);
    write_be16(&udp[2], dst_port);
    write_be16(&udp[4], udp_len);
    write_be16(&udp[6], 0u);
    if (len > 0u) {
        mem_copy(&udp[NET_UDP_HDR_SIZE], data, len);
    }

    frame_len = NET_ETH_HDR_SIZE + NET_IP_HDR_SIZE + NET_UDP_HDR_SIZE + len;
    if (e1000_send_frame(frame, (uint16_t)frame_len) != 0) {
        g_stats.dropped_packets++;
        return -1;
    }

    g_stats.tx_packets++;
    return (int)len;
}

int net_info(myaos_netinfo_t* out) {
    uint32_t socket_count = 0u;
    uint32_t bound_ports = 0u;

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
