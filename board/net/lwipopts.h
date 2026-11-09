#pragma once

// lwIP configuration for Richie: bare metal (NO_SYS), raw callback API only,
// polled from the panda main loop. Frames are copied between the Ethernet DMA
// buffers (SRAM1/2) and the lwIP heap, so lwIP memory lives in DTCM.
// Only settings that differ from lwIP's defaults (lwip/opt.h) are listed.

// ---------- Platform ----------
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_SINGLE_NETIF               1
#define MEM_ALIGNMENT                   4

// Word-wise copy that stays fast on unaligned frame data (board/net/net_libc.c)
#include <stddef.h>
void *net_memcpy(void *dest, const void *src, size_t n);
#define MEMCPY(dst, src, len)           net_memcpy((dst), (src), (len))
#define SMEMCPY(dst, src, len)          net_memcpy((dst), (src), (len))

// ---------- Memory (~10 KB of DTCM) ----------
// One heap for everything: received frames (PBUF_RAM, see net.c) and queued
// transmit data share it, so no pbuf pool is reserved. Measured peak with one
// TCP stream at full speed: 4.7 KB. Overload degrades to drops/retransmits.
#define MEM_SIZE                        (8 * 1024)
#define PBUF_POOL_SIZE                  0
#define MEMP_NUM_PBUF                   4
#define MEMP_NUM_TCP_PCB                2
#define MEMP_NUM_TCP_PCB_LISTEN         2

// ---------- Link layer (a car's diagnostic link has few neighbors) ----------
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_EXT_STATUS_CALLBACK  1
#define ARP_TABLE_SIZE                  4

// ---------- IPv4: always start with AutoIP/APIPA link-local ----------
#define LWIP_IPV4                       1
#define LWIP_AUTOIP                     1
#define LWIP_DHCP                       1  // available for later configuration; not started at boot
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0

// ---------- IPv6: link-local + SLAAC, concurrent with IPv4 AutoIP ----------
#define LWIP_IPV6                       1
#define LWIP_IPV6_FRAG                  0
#define LWIP_IPV6_REASS                 0
#define LWIP_ND6_NUM_NEIGHBORS          4
#define LWIP_ND6_NUM_DESTINATIONS       4
#define LWIP_MULTICAST_PING             1

// ---------- TCP ----------
// Two-segment windows: ~36 Mbit/s per connection, far above what DoIP can
// forward to CAN, at half the heap of 4-segment windows.
#define TCP_MSS                         1460
#define TCP_WND                         (2 * TCP_MSS)
#define TCP_SND_BUF                     (2 * TCP_MSS)
// Drop out-of-order segments (the sender retransmits; rare on a direct link)
// so they can never pin heap that received frames need.
#define TCP_QUEUE_OOSEQ                 0

// ---------- Checksums ----------
// The MAC inserts IPv4 header and TCP/UDP checksums on transmit (TDES3 CIC).
// ICMP checksums and all receive-side checks stay in software (lwIP default).
#define CHECKSUM_GEN_IP                 0
#define CHECKSUM_GEN_UDP                0
#define CHECKSUM_GEN_TCP                0

// ---------- Debug (LWIP_DEBUG stays undefined: lwIP tests it with #ifdef) ----------
#define LWIP_STATS                      0
