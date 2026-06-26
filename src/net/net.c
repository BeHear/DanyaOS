#include "net.h"
#include "../drivers/ne2000.h"
#include "../drivers/vga.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../libc/string.h"
#include "../memory/heap.h"
#include "../include/io.h"

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
static uint8_t netmask[4]   = {255, 255, 255, 0};
static uint8_t gateway[4]   = {10, 0, 2, 2};

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
    /* Route: if dst is not on local subnet, resolve gateway instead */
    uint8_t route_ip[4];
    int on_subnet = 1;
    for (int i = 0; i < 4; i++)
        if ((dst_ip[i] & netmask[i]) != (our_ip[i] & netmask[i]))
            on_subnet = 0;
    if (on_subnet) {
        memcpy(route_ip, dst_ip, 4);
    } else {
        memcpy(route_ip, gateway, 4);
    }
    if (!arp_resolve(route_ip, mac)) return -1;

    uint16_t total = 14 + 20 + len;
    if (total > 1500) return -1;
    uint8_t frame[1500];
    eth_header_t* eth = (eth_header_t*)frame;
    memcpy(eth->dst_mac, mac, 6);
    ne2000_get_mac(eth->src_mac);
    eth->ethertype = htons(ETH_TYPE_IP);

    ip_header_t* ip = (ip_header_t*)(frame + 14);
    memset(ip, 0, 20);
    ip->hdr_len    = 0x45; /* version=4, IHL=5 (20 bytes) */
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
    serial_puts("[arp] send req "); serial_puts_hex(ip[0]); serial_puts("."); serial_puts_hex(ip[1]); serial_puts("."); serial_puts_hex(ip[2]); serial_puts("."); serial_puts_hex(ip[3]); serial_puts("\n");
    ne2000_send(pkt, 42);
    serial_puts("[arp] sent\n");
    uint32_t deadline = timer_get_ticks() + 100;
    while (arp_waiting && timer_get_ticks() < deadline) {
        net_poll();
        asm volatile("sti; hlt; cli");
    }
    if (!arp_waiting) { uint8_t tmp[6]; cli(); memcpy(tmp, (uint8_t*)arp_reply_mac, 6); sti(); memcpy(mac, tmp, 6); arp_cache_add(ip, mac); serial_puts("[arp] OK\n"); return 1; }
    serial_puts("[arp] TIMEOUT\n");
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
        uint8_t tmp[6]; memcpy(tmp, a->src_mac, 6);
        cli(); memcpy((uint8_t*)arp_reply_mac, tmp, 6); sti();
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
volatile uint32_t icmp_recv_count;
volatile uint8_t last_icmp_type;

void icmp_process(const uint8_t* payload, uint16_t len, const uint8_t* src) {
    if (len < 8) return;
    icmp_header_t* icmp = (icmp_header_t*)payload;
    icmp_recv_count++;
    last_icmp_type = icmp->type;
    if (icmp->type == 0) { /* echo reply */
        if (icmp->id == htons(ping_id) || icmp->id == ping_id) {
            ping_ok = 1;
            return;
        }
    }
    if (icmp->type == 8) {
        uint8_t* buf = (uint8_t*)kmalloc(len);
        if (!buf) return;
        memcpy(buf, payload, len);
        icmp_header_t* r = (icmp_header_t*)buf;
        r->type = 0;
        r->checksum = 0;
        r->checksum = ip_checksum(buf, len);
        net_send_ip(src, IP_ICMP, buf, len);
        kfree(buf);
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
static uint16_t tcp_eph_port = 0xC000; // ephemeral port counter

static void tcp_send_seg(uint8_t flags, const uint8_t* data, uint16_t dlen) {
    uint16_t seglen = 20 + dlen;
    if (seglen > 1500) return;
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
        }
        {
            uint32_t inc = dlen;
            if (h->flags & TCP_FLAG_FIN) inc++;
            if (inc) {
                ts.ack = ntohl(h->seq_num) + inc;
                tcp_send_seg(TCP_FLAG_ACK, 0, 0);
            }
        }
        if (h->flags & TCP_FLAG_FIN) ts.state = TCP_CLOSED;
        break;
    default: break;
    }
}

/* ═══════════════════════════════════════════
   Public TCP API
   ═══════════════════════════════════════════ */
int tcp_connect(const char* host, uint16_t port) {
    int a=0, b=0, c=0, d=0;
    const char* p = host, *orig = host;
    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
    // DNS fallback if not a valid dotted IP
    if (*p != '\0' || (a == 0 && b == 0 && c == 0 && d == 0 && *orig != '0')) {
        uint8_t dns_ip[4];
        if (dns_resolve(host, dns_ip) != 0) return -2;
        a = dns_ip[0]; b = dns_ip[1]; c = dns_ip[2]; d = dns_ip[3];
    }
    tcp_dst_ip[0] = (uint8_t)a; tcp_dst_ip[1] = (uint8_t)b;
    tcp_dst_ip[2] = (uint8_t)c; tcp_dst_ip[3] = (uint8_t)d;
    ts.sport = tcp_eph_port++;
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
    // If already closed with no data, return -1
    if (ts.state == TCP_CLOSED && ts.rx_read >= ts.rx_len) return -1;
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
        if (ts.state == TCP_CLOSED) return -1;
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
   UDP
   ═══════════════════════════════════════════ */
#define UDP_BUF_SZ 1536
static uint8_t  udp_buf[UDP_BUF_SZ];
static uint16_t udp_len;
static uint16_t udp_port;
static volatile int udp_got;

static void udp_process(const uint8_t* payload, uint16_t len) {
    if (len < 8) return;
    const udp_header_t* uh = (const udp_header_t*)payload;
    uint16_t dport = ntohs(uh->dst_port);
    uint16_t dlen  = ntohs(uh->len);
    if (dlen < 8 || dlen - 8 > UDP_BUF_SZ) return;
    dlen -= 8;
    if (!udp_got && dport == udp_port) {
        memcpy(udp_buf, payload + 8, dlen);
        udp_len = dlen;
        udp_got = 1;
    }
}

int udp_send(const uint8_t* dst_ip, uint16_t dst_port, uint16_t src_port,
             const uint8_t* data, uint16_t len) {
    uint8_t buf[512];
    uint16_t total = 8 + len;
    if (total > 512) return -1;
    udp_header_t* uh = (udp_header_t*)buf;
    uh->src_port = htons(src_port);
    uh->dst_port = htons(dst_port);
    uh->len      = htons(total);
    uh->checksum = 0;
    if (data && len) memcpy(buf + 8, data, len);
    return net_send_ip(dst_ip, IP_UDP, buf, total);
}

int udp_recv(uint16_t port, uint8_t* buf, uint16_t max) {
    udp_port = port;
    udp_got  = 0;
    uint32_t deadline = timer_get_ticks() + 50;
    while (timer_get_ticks() < deadline) {
        net_poll();
        if (udp_got) {
            uint16_t n = udp_len > max ? max : udp_len;
            memcpy(buf, udp_buf, n);
            udp_got = 0;
            return (int)n;
        }
        asm volatile("sti; hlt; cli");
    }
    return -1;
}

/* ═══════════════════════════════════════════
   DNS
   ═══════════════════════════════════════════ */
static int dns_enc_name(uint8_t* dst, const char* name) {
    int pos = 0;
    while (*name) {
        const char* dot = name;
        while (*dot && *dot != '.') dot++;
        int n = (int)(dot - name);
        if (n > 63) return -1;
        dst[pos++] = (uint8_t)n;
        for (int i = 0; i < n; i++) dst[pos++] = (uint8_t)*name++;
        if (*name == '.') name++;
    }
    dst[pos++] = 0;
    return pos;
}

static const uint8_t* dns_skip_name(const uint8_t* msg, const uint8_t* p) {
    (void)msg;
    while (*p) {
        if (*p & 0xC0) return p + 2;
        p += *p + 1;
    }
    return p + 1;
}

int dns_resolve(const char* hostname, uint8_t* ip_out) {
    uint8_t dns_ip[4] = {10, 0, 2, 3};
    uint16_t src_port = tcp_eph_port++;
    uint16_t id = 0x1234;

    uint8_t query[256];
    query[0] = (uint8_t)(id >> 8); query[1] = (uint8_t)(id & 0xFF);
    query[2] = 0x01; query[3] = 0x00;
    query[4] = 0; query[5] = 1;
    memset(query + 6, 0, 6);

    uint8_t enc[128];
    int enclen = dns_enc_name(enc, hostname);
    if (enclen < 0) return -5;
    memcpy(query + 12, enc, enclen);
    int off = 12 + enclen;
    query[off++] = 0; query[off++] = 1;
    query[off++] = 0; query[off++] = 1;

    if (udp_send(dns_ip, 53, src_port, query, off) != 0) return -1;

    uint8_t reply[512];
    uint32_t deadline = timer_get_ticks() + 100;
    while (timer_get_ticks() < deadline) {
        int n = udp_recv(src_port, reply, sizeof(reply));
        if (n > 12 && reply[0] == query[0] && reply[1] == query[1]) {
            if ((reply[2] & 0x80) && (reply[3] & 0x0F) == 0) {
                int ancount = (reply[6] << 8) | reply[7];
                if (ancount > 0) {
                    const uint8_t* p = reply + 12;
                    p = dns_skip_name(reply, p); p += 4;
                    p = dns_skip_name(reply, p);
                    uint16_t type = (p[0] << 8) | p[1];
                    if (type == 1) {
                        p += 8;  // skip TYPE(2) + CLASS(2) + TTL(4)
                        uint16_t rdlen = (p[0] << 8) | p[1]; p += 2;
                        if (rdlen == 4) {
                            ip_out[0] = p[0]; ip_out[1] = p[1];
                            ip_out[2] = p[2]; ip_out[3] = p[3];
                            return 0;
                        }
                    }
                    return -4;
                }
                return -3;
            }
            return -2;
        }
        asm volatile("sti; hlt; cli");
    }
    return -1;
}

/* ═══════════════════════════════════════════
   Packet dispatch
   ═══════════════════════════════════════════ */
static void dispatch(const uint8_t* pkt, uint16_t len) {
    if (len < 14) return;
    const eth_header_t* e = (const eth_header_t*)pkt;
    uint16_t type = ntohs(e->ethertype);
    serial_puts("[rx] type=0x"); serial_puts_hex(type>>8); serial_puts_hex(type&0xFF); serial_puts(" len="); serial_puts_hex(len>>8); serial_puts_hex(len&0xFF); serial_puts("\n");
    if (type == ETH_TYPE_ARP) { arp_process(pkt, len); return; }
    if (type != ETH_TYPE_IP) return;
    if (len < 34) return;
    const ip_header_t* ip = (const ip_header_t*)(pkt + 14);
    if (ip->proto == IP_ICMP) {
        icmp_process(pkt + 34, len - 34, ip->src_ip);
    } else if (ip->proto == IP_TCP) {
        tcp_process(pkt + 34, len - 34, ip->src_ip);
    } else if (ip->proto == IP_UDP) {
        udp_process(pkt + 34, len - 34);
    }
}

void net_init(void) {
    rx_head = rx_tail = 0;
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(&ts, 0, sizeof(ts));
    ts.state = TCP_CLOSED;
    udp_got = 0;
    vga_puts("  [ OK ] Network stack (ARP/IP/ICMP/TCP/UDP/DNS)\n");
}

void net_poll(void) {
    ne2000_rx_packet_t p;
    while (ne2000_poll(&p)) net_rx(p.data, p.len);
    while (rx_head != rx_tail) {
        dispatch(rx_buf[rx_head], rx_len[rx_head]);
        rx_head = (rx_head + 1) % RX_RING;
    }
}
