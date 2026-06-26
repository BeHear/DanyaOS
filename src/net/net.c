#include "net.h"
#include "../drivers/ne2000.h"
#include "../drivers/vga.h"
#include "../drivers/timer.h"
#include "../libc/string.h"
#include "../memory/heap.h"

/* ═══════════════════════════════════════════
   Byte order helpers
   ═══════════════════════════════════════════ */
static uint16_t htons(uint16_t x) { return (x >> 8) | (x << 8); }
static uint16_t ntohs(uint16_t x) { return (x >> 8) | (x << 8); }
static uint32_t htonl(uint32_t x) {
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
           ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
}
static uint32_t ntohl(uint32_t x) { return htonl(x); }

/* ═══════════════════════════════════════════
   Configuration
   ═══════════════════════════════════════════ */
static uint8_t our_ip[4]     = {10, 0, 2, 15};

/* ═══════════════════════════════════════════
   RX ring buffer
   ═══════════════════════════════════════════ */
#define RX_RING 16
static uint8_t  rx_buf[RX_RING][1514];
static uint16_t rx_len[RX_RING];
static volatile int rx_head, rx_tail;

void net_rx(const uint8_t* buf, uint16_t len) {
    int n = (rx_tail + 1) % RX_RING;
    if (n == rx_head) return;
    memcpy(rx_buf[rx_tail], buf, len);
    rx_len[rx_tail] = len;
    rx_tail = n;
}

/* ═══════════════════════════════════════════
   Checksums
   ═══════════════════════════════════════════ */
uint16_t ip_checksum(const void* buf, uint16_t len) {
    const uint16_t* p = (const uint16_t*)buf;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t tcp_checksum(const uint8_t* src, const uint8_t* dst,
                      const uint8_t* data, uint16_t len) {
    uint8_t pseudo[12];
    memcpy(pseudo + 0, src, 4);
    memcpy(pseudo + 4, dst, 4);
    pseudo[8] = 0; pseudo[9] = 6;
    pseudo[10] = (uint8_t)(len >> 8);
    pseudo[11] = (uint8_t)(len & 0xFF);
    uint8_t* tmp = (uint8_t*)kmalloc(12 + len);
    if (!tmp) return 0;
    memcpy(tmp, pseudo, 12);
    memcpy(tmp + 12, data, len);
    uint16_t r = ip_checksum(tmp, 12 + len);
    kfree(tmp);
    return r;
}

/* ═══════════════════════════════════════════
   Send raw IP
   ═══════════════════════════════════════════ */
int net_send_ip(const uint8_t* dst_ip, uint8_t proto,
                const uint8_t* payload, uint16_t len) {
    uint8_t mac[6];
    if (!arp_resolve(dst_ip, mac)) return -1;

    uint8_t frame[1500];
    eth_header_t* eth = (eth_header_t*)frame;
    memcpy(eth->dst_mac, mac, 6);
    ne2000_get_mac(eth->src_mac);
    eth->ethertype = htons(ETH_TYPE_IP);

    ip_header_t* ip = (ip_header_t*)(frame + 14);
    memset(ip, 0, 20);
    ip->hdr_len    = 5;
    ip->total_len  = htons(20 + len);
    ip->ttl        = 64;
    ip->proto      = proto;
    memcpy(ip->src_ip, our_ip, 4);
    memcpy(ip->dst_ip, dst_ip, 4);
    memcpy(frame + 34, payload, len);
    ip->checksum = 0;
    ip->checksum = ip_checksum(frame + 14, 20);

    ne2000_send(frame, 14 + 20 + len);
    return 0;
}

/* ═══════════════════════════════════════════
   ARP cache
   ═══════════════════════════════════════════ */
#define ARP_CACHE_N 8
typedef struct { uint8_t ip[4]; uint8_t mac[6]; uint8_t valid; } arp_cache_t;
static arp_cache_t arp_cache[ARP_CACHE_N];
static volatile int arp_waiting;
static volatile uint8_t arp_reply_mac[6];

static void arp_cache_add(const uint8_t* ip, const uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_N; i++) {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_N; i++) {
        if (!arp_cache[i].valid) {
            memcpy(arp_cache[i].ip, ip, 4);
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            return;
        }
    }
}

int arp_resolve(const uint8_t* ip, uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_N; i++) {
        if (arp_cache[i].valid && memcmp(arp_cache[i].ip, ip, 4) == 0) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    // send ARP request
    uint8_t pkt[42];
    eth_header_t* e = (eth_header_t*)pkt;
    memset(e->dst_mac, 0xFF, 6);
    ne2000_get_mac(e->src_mac);
    e->ethertype = htons(ETH_TYPE_ARP);
    arp_packet_t* a = (arp_packet_t*)(pkt + 14);
    a->hw_type = htons(1); a->proto_type = htons(0x0800);
    a->hw_len = 6; a->proto_len = 4;
    a->op = htons(1);
    ne2000_get_mac(a->src_mac);
    memcpy(a->src_ip, our_ip, 4);
    memset(a->dst_mac, 0, 6);
    memcpy(a->dst_ip, ip, 4);

    arp_waiting = 1;
    ne2000_send(pkt, 42);
    uint32_t deadline = timer_get_ticks() + 100;
    while (arp_waiting && timer_get_ticks() < deadline) {
        net_poll();
        asm volatile("sti; hlt; cli");
        net_poll(); /* Check again after hlt */
    }
    if (!arp_waiting) { memcpy(mac, arp_reply_mac, 6); arp_cache_add(ip, mac); return 1; }
    return 0;
}

static void arp_process(const uint8_t* pkt, uint16_t len) {
    if (len < 42) return;
    arp_packet_t* a = (arp_packet_t*)(pkt + 14);
    if (a->op == htons(1) && memcmp(a->dst_ip, our_ip, 4) == 0) {
        uint8_t reply[42];
        eth_header_t* e = (eth_header_t*)reply;
        memcpy(e->dst_mac, a->src_mac, 6);
        ne2000_get_mac(e->src_mac);
        e->ethertype = htons(ETH_TYPE_ARP);
        arp_packet_t* r = (arp_packet_t*)(reply + 14);
        r->hw_type = htons(1); r->proto_type = htons(0x0800);
        r->hw_len = 6; r->proto_len = 4;
        r->op = htons(2);
        ne2000_get_mac(r->src_mac);
        memcpy(r->src_ip, our_ip, 4);
        memcpy(r->dst_mac, a->src_mac, 6);
        memcpy(r->dst_ip, a->src_ip, 4);
        ne2000_send(reply, 42);
        arp_cache_add(a->src_ip, a->src_mac);
    }
    if (a->op == htons(2) && arp_waiting) {
        memcpy(arp_reply_mac, a->src_mac, 6);
        arp_cache_add(a->src_ip, a->src_mac);
        arp_waiting = 0;
    }
}

/* ═══════════════════════════════════════════
   ICMP
   ═══════════════════════════════════════════ */
static uint16_t ping_id;
static volatile int ping_ok;
static volatile uint32_t ping_rtt;

void icmp_process(const uint8_t* payload, uint16_t len, const uint8_t* src) {
    if (len < 8) return;
    icmp_header_t* icmp = (icmp_header_t*)payload;
    if (icmp->type == 0 && icmp->id == htons(ping_id)) { ping_ok = 1; return; }
    if (icmp->type == 8) {
        uint8_t buf[64];
        memset(buf, 0, 64);
        memcpy(buf, payload, len < 64 ? len : 64);
        icmp_header_t* r = (icmp_header_t*)buf;
        r->type = 0;
        r->checksum = 0;
        r->checksum = ip_checksum(buf, len < 64 ? len : 64);
        net_send_ip(src, IP_ICMP, buf, len < 64 ? len : 64);
    }
}

int net_ping(uint32_t ip) {
    uint8_t dst[4] = {(uint8_t)(ip>>24),(uint8_t)(ip>>16),(uint8_t)(ip>>8),(uint8_t)ip};
    uint8_t pkt[64];
    memset(pkt, 0, 64);
    icmp_header_t* icmp = (icmp_header_t*)pkt;
    icmp->type = 8; icmp->code = 0;
    ping_id++; icmp->id = htons(ping_id); icmp->seq = htons(1);
    icmp->checksum = ip_checksum(pkt, 64);

    ping_ok = 0;
    uint32_t t0 = timer_get_ticks();
    if (net_send_ip(dst, IP_ICMP, pkt, 64) != 0) return -1;

    uint32_t deadline = t0 + 50; /* 500ms timeout */
    while (!ping_ok && timer_get_ticks() < deadline) {
        net_poll();
        asm volatile("sti; hlt; cli");
    }
    if (ping_ok) { ping_rtt = timer_get_ticks() - t0; return (int)ping_rtt; }
    return -2;
}

/* ═══════════════════════════════════════════
   TCP
   ═══════════════════════════════════════════ */
#define TCP_CLOSED      0
#define TCP_SYN_SENT    1
#define TCP_ESTABLISHED 2
#define TCP_FIN_WAIT    3
#define TCP_CLOSING     4

typedef struct {
    uint32_t state;
    uint16_t sport, dport;
    uint32_t seq, ack;
    uint8_t  rx_buf[8192];
    uint32_t rx_len, rx_read;
} tcp_state_t;

static tcp_state_t ts;
static uint8_t tcp_dst_ip[4];

static void tcp_send_seg(uint8_t flags, const uint8_t* data, uint16_t dlen) {
    uint8_t buf[1500];
    tcp_header_t* h = (tcp_header_t*)buf;
    memset(h, 0, 20);
    h->src_port = htons(ts.sport);
    h->dst_port = htons(ts.dport);
    h->seq_num  = htonl(ts.seq);
    h->ack_num  = htonl(ts.ack);
    h->data_off = 0x50;
    h->flags    = flags;
    h->window   = htons(0x1000);
    if (dlen) memcpy(buf + 20, data, dlen);
    h->checksum = 0;
    h->checksum = tcp_checksum(our_ip, tcp_dst_ip, buf, 20 + dlen);

    net_send_ip(tcp_dst_ip, IP_TCP, buf, 20 + dlen);
    if (flags & TCP_FLAG_SYN) ts.seq++;
    if (flags & TCP_FLAG_FIN) ts.seq++;
    ts.seq += dlen;
}

void tcp_process(const uint8_t* payload, uint16_t len, const uint8_t* src_ip) {
    (void)src_ip;
    if (len < 20) return;
    tcp_header_t* h = (tcp_header_t*)payload;
    if (ntohs(h->dst_port) != ts.sport) return;
    uint16_t data_off = ((h->data_off >> 4) & 0xF) * 4;
    uint16_t dlen = len - data_off;

    switch (ts.state) {
    case TCP_SYN_SENT:
        if ((h->flags & TCP_FLAG_SYN) && (h->flags & TCP_FLAG_ACK)) {
            ts.ack = ntohl(h->seq_num) + 1;
            ts.state = TCP_ESTABLISHED;
            tcp_send_seg(TCP_FLAG_ACK, 0, 0);
        }
        break;
    case TCP_ESTABLISHED:
        if (h->flags & TCP_FLAG_RST) { ts.state = TCP_CLOSED; break; }
        if (dlen > 0) {
            if (ts.rx_len + dlen < sizeof(ts.rx_buf)) {
                memcpy(ts.rx_buf + ts.rx_len, payload + data_off, dlen);
                ts.rx_len += dlen;
            }
            ts.ack = ntohl(h->seq_num) + dlen;
            tcp_send_seg(TCP_FLAG_ACK, 0, 0);
        }
        if (h->flags & TCP_FLAG_FIN) {
            ts.ack = ntohl(h->seq_num) + 1;
            tcp_send_seg(TCP_FLAG_ACK, 0, 0);
            ts.state = TCP_CLOSED;
        }
        break;
    default: break;
    }
}

/* ═══════════════════════════════════════════
   Public TCP API
   ═══════════════════════════════════════════ */
int tcp_connect(const char* host, uint16_t port) {
    int a=0, b=0, c=0, d=0;
    // Simple IP parser (no sscanf in freestanding)
    const char* p = host;
    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
    tcp_dst_ip[0] = a; tcp_dst_ip[1] = b; tcp_dst_ip[2] = c; tcp_dst_ip[3] = d;
    ts.sport = 0xC000 + port;
    ts.dport = port;
    ts.seq = 0x1000;
    ts.ack = 0;
    ts.state = TCP_SYN_SENT;
    ts.rx_len = ts.rx_read = 0;

    tcp_send_seg(TCP_FLAG_SYN, 0, 0);
    uint32_t deadline = timer_get_ticks() + 50;
    while (ts.state == TCP_SYN_SENT && timer_get_ticks() < deadline) {
        net_poll();
        asm volatile("sti; hlt; cli");
    }
    if (ts.state == TCP_ESTABLISHED) return 0;
    ts.state = TCP_CLOSED;
    return -1;
}

int tcp_send_data(const uint8_t* data, uint16_t len) {
    if (ts.state != TCP_ESTABLISHED) return -1;
    tcp_send_seg(TCP_FLAG_ACK | TCP_FLAG_PSH, data, len);
    return (int)len;
}

int tcp_recv(uint8_t* buf, uint16_t max) {
    uint32_t deadline = timer_get_ticks() + 50;
    while (timer_get_ticks() < deadline) {
        net_poll();
        if (ts.rx_read < ts.rx_len) {
            uint32_t avail = ts.rx_len - ts.rx_read;
            uint32_t n = avail > max ? max : avail;
            memcpy(buf, ts.rx_buf + ts.rx_read, n);
            ts.rx_read += n;
            return (int)n;
        }
        asm volatile("sti; hlt; cli");
    }
    return 0;
}

void tcp_close(void) {
    if (ts.state == TCP_ESTABLISHED || ts.state == TCP_FIN_WAIT) {
        tcp_send_seg(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
        ts.state = TCP_CLOSING;
        uint32_t deadline = timer_get_ticks() + 30;
        while (ts.state != TCP_CLOSED && timer_get_ticks() < deadline) {
            net_poll();
            asm volatile("sti; hlt; cli");
        }
    }
    ts.state = TCP_CLOSED;
}

/* ═══════════════════════════════════════════
   Packet dispatch
   ═══════════════════════════════════════════ */
static void dispatch(const uint8_t* pkt, uint16_t len) {
    if (len < 14) return;
    const eth_header_t* e = (const eth_header_t*)pkt;
    if (e->ethertype == ETH_TYPE_ARP) { arp_process(pkt, len); return; }
    if (e->ethertype != ETH_TYPE_IP) return;
    if (len < 34) return;
    const ip_header_t* ip = (const ip_header_t*)(pkt + 14);
    if (ip->proto == IP_ICMP)
        icmp_process(pkt + 34, len - 34, ip->src_ip);
    else if (ip->proto == IP_TCP)
        tcp_process(pkt + 34, len - 34, ip->src_ip);
}

/* ═══════════════════════════════════════════
   net_init / net_poll
   ═══════════════════════════════════════════ */
void net_init(void) {
    rx_head = rx_tail = 0;
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(&ts, 0, sizeof(ts));
    ts.state = TCP_CLOSED;
    vga_puts("  [ OK ] Network stack (ARP/IP/ICMP/TCP)\n");
}

void net_poll(void) {
    ne2000_rx_packet_t p;
    while (ne2000_poll(&p)) net_rx(p.data, p.len);
    while (rx_head != rx_tail) {
        dispatch(rx_buf[rx_head], rx_len[rx_head]);
        rx_head = (rx_head + 1) % RX_RING;
    }
}
