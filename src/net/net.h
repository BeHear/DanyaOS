#ifndef DANYA_NET_H
#define DANYA_NET_H

#include "../include/types.h"

// Ethernet
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

// ARP
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

// IP
#define IP_ICMP 1
#define IP_TCP  6

// ICMP
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQ   8

// TCP flags
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

// Packet headers
typedef struct {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} __attribute__((packed)) eth_header_t;

typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t op;
    uint8_t  src_mac[6];
    uint8_t  src_ip[4];
    uint8_t  dst_mac[6];
    uint8_t  dst_ip[4];
} __attribute__((packed)) arp_packet_t;

typedef struct {
    uint8_t  hdr_len;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_offset;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
} __attribute__((packed)) ip_header_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_header_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_off;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urg;
} __attribute__((packed)) tcp_header_t;

// UDP
#define IP_UDP 17

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

int udp_send(const uint8_t* dst_ip, uint16_t dst_port, uint16_t src_port,
             const uint8_t* data, uint16_t len);
int udp_recv(uint16_t port, uint8_t* buf, uint16_t max);
int dns_resolve(const char* hostname, uint8_t* ip_out);

// --- API ---
void net_init(void);
void net_poll(void);
int  net_send_ip(const uint8_t* dst_ip, uint8_t proto, const uint8_t* payload, uint16_t len);
int  arp_resolve(const uint8_t* ip, uint8_t* mac);
uint16_t ip_checksum(const void* buf, uint16_t len);
uint16_t tcp_checksum(const uint8_t* src_ip, const uint8_t* dst_ip, const uint8_t* data, uint16_t len);

// ICMP
int  net_ping(uint32_t ip);
extern volatile uint32_t icmp_recv_count;
extern volatile uint8_t last_icmp_type;

// TCP
int  tcp_connect(const char* host, uint16_t port);
int  tcp_send_data(const uint8_t* data, uint16_t len);
int  tcp_recv(uint8_t* buf, uint16_t max);
void tcp_close(void);

// Driver calls this when packet arrives
void net_rx(const uint8_t* buf, uint16_t len);

#endif
