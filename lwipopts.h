// lwipopts.h - minimal lwIP options for Pico W / Pico 2W
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// We are not using an OS, but Pico SDK provides a threadsafe arch wrapper.
#define NO_SYS                      1

// Memory options
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)

// Pbuf options
#define PBUF_POOL_SIZE              8
#define PBUF_POOL_BUFSIZE           1700

// TCP options
#define LWIP_TCP                    1
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)
#define MEMP_NUM_TCP_SEG         ((4 * TCP_SND_BUF) / TCP_MSS)

// UDP / ICMP / DHCP / DNS
#define LWIP_UDP                    1
#define LWIP_ICMP                   1
#define LWIP_DHCP                   1
#define LWIP_DNS                    1

// Sockets layer (optional). We’re using raw TCP API in the sample, so not required.
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

// Debugging (set to 1 if you want lwIP stats/counters)
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

// Checksum handling
#define LWIP_CHECKSUM_CTRL_PER_NETIF 1

// Required by Pico SDK’s cyw43/lwIP glue in many examples
#define LWIP_NETIF_HOSTNAME         1

#endif /* _LWIPOPTS_H */
