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

// ---------- Memory (~16 KB of DTCM) ----------
// One heap for everything: received frames (PBUF_RAM, see net.c) and queued
// transmit data share it, so no pbuf pool is reserved.
#define MEM_SIZE                        (14 * 1024)
#define PBUF_POOL_SIZE                  0
#define MEMP_NUM_PBUF                   4
#define MEMP_NUM_TCP_PCB                4
#define MEMP_NUM_TCP_PCB_LISTEN         2

// ---------- Link layer (a car's diagnostic link has few neighbors) ----------
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_EXT_STATUS_CALLBACK  1
#define ARP_TABLE_SIZE                  4

// ---------- IPv4: DHCP, falling back to AutoIP link-local ----------
#define LWIP_DHCP                       1
#define LWIP_AUTOIP                     1
#define LWIP_DHCP_AUTOIP_COOP           1
#define LWIP_DHCP_AUTOIP_COOP_TRIES     3
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0

// ---------- IPv6: link-local + SLAAC ----------
#define LWIP_IPV6                       1
#define LWIP_IPV6_FRAG                  0
#define LWIP_IPV6_REASS                 0
#define LWIP_ND6_NUM_NEIGHBORS          4
#define LWIP_ND6_NUM_DESTINATIONS       4
#define LWIP_MULTICAST_PING             1

// ---------- TCP ----------
#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)

// ---------- Checksums ----------
// The MAC inserts IPv4 header and TCP/UDP checksums on transmit (TDES3 CIC).
// ICMP checksums and all receive-side checks stay in software (lwIP default).
#define CHECKSUM_GEN_IP                 0
#define CHECKSUM_GEN_UDP                0
#define CHECKSUM_GEN_TCP                0

// ---------- Debug (LWIP_DEBUG stays undefined: lwIP tests it with #ifdef) ----------
#define LWIP_STATS                      0
