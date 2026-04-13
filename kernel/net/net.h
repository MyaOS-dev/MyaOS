#ifndef NET_H
#define NET_H

#include <myaos/syscall.h>
#include <stdint.h>

void net_init(void);
int net_reprobe(void);
int net_socket_open_ex(int32_t owner_pid, uint8_t proto, uint16_t local_port, int32_t* out_fd);
int net_socket_open(int32_t owner_pid, uint16_t local_port, int32_t* out_fd);
int net_socket_bind(int32_t owner_pid, int32_t fd, uint16_t local_port);
int net_socket_listen(int32_t owner_pid, int32_t fd, uint16_t backlog);
int net_socket_accept(int32_t owner_pid, int32_t fd, int32_t* out_fd, uint16_t* out_peer_port);
int net_socket_connect(int32_t owner_pid, int32_t fd, uint16_t dst_port);
int net_socket_connect4(int32_t owner_pid, int32_t fd, uint32_t dst_ip, uint16_t dst_port);
int net_socket_sendto(
    int32_t owner_pid,
    int32_t fd,
    uint16_t dst_port,
    const void* data,
    uint32_t len,
    uint32_t* out_written
);
int net_socket_recvfrom(
    int32_t owner_pid,
    int32_t fd,
    void* out_buf,
    uint32_t max_len,
    uint32_t* out_len,
    uint16_t* out_src_port
);
int net_send_udp4(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const void* data, uint32_t len);
int net_socket_close(int32_t owner_pid, int32_t fd);
int net_socket_send(
    int32_t owner_pid,
    int32_t fd,
    uint16_t dst_port,
    const void* data,
    uint32_t len,
    uint32_t* out_written
);
int net_socket_recv(
    int32_t owner_pid,
    int32_t fd,
    void* out_buf,
    uint32_t max_len,
    uint32_t* out_len,
    uint16_t* out_src_port
);
int net_info(myaos_netinfo_t* out);
int net_ping4(uint32_t dst_ip, uint16_t ident, uint16_t seq, uint32_t timeout_polls, uint32_t* out_rtt_ticks);
uint32_t net_default_src_ip(void);
uint32_t net_default_gateway_ip(void);
uint32_t net_default_dns_ip(void);
const char* net_if_name(void);
const char* net_driver_name(void);
const char* net_nic_model(void);
const char* net_nic_state(void);
const char* net_nic_note(void);
const char* net_nic_firmware_state(void);
const char* net_nic_firmware_path(void);
const char* net_nic_pci_bdf(void);
const char* net_nic_pci_id(void);
uint32_t net_nic_detected_count(void);
uint32_t net_nic_supported_count(void);
void net_on_process_exit(int32_t pid);

#endif
