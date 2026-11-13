/*
 * lwipopts.h - lwIP configuration for the USB network link.
 *
 * NO_SYS, raw API only. Sized for a handful of TCP clients (NDJSON on :9000
 * and HTTP on :80). IPv4 on both links; IPv6 link-local on the Ethernet side
 * only, for finding and reading other testers with mDNS (remote.h).
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_TIMERS                     1

#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (24 * 1024)   /* each pushed status is copied per client */
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                8      /* dns, mdns, dhcp client, dhcp server + headroom */
#define MEMP_NUM_TCP_PCB                16     /* 4 HTTP + 4 :9000 + 4 WebSocket + closing ones */
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                48
/* lwIP's computed default (LWIP_NUM_SYS_TIMEOUT_INTERNAL) covers TCP, ARP,
 * DHCP, DNS and IGMP but NOT the mDNS responder, which arms several timers
 * per netif at link-up; with the default the pool ran dry and lwIP hit
 * "sys_timeout: timeout != NULL, pool MEMP_SYS_TIMEOUT is empty". */
#define MEMP_NUM_SYS_TIMEOUT            32     /* + mDNS on the port, ND6, MLD */
#define MEMP_NUM_RAW_PCB                2
#define PBUF_POOL_SIZE                  16

#define LWIP_RAW                        1      /* ICMP echo for the port probe   */
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_DHCP                       1      /* client on the Ethernet netif   */
#define LWIP_DHCP_CHECK_LINK_UP         1
#define LWIP_DNS                        1      /* resolver for the port probe    */
#define DNS_MAX_SERVERS                 2
#define DNS_TABLE_SIZE                  2
#define LWIP_DNS_ADDRTYPE_DEFAULT       0      /* LWIP_DNS_ADDRTYPE_IPV4: tools and probe are IPv4 */
#define LWIP_IGMP                       1      /* mDNS needs multicast           */
#ifndef USB_MDNS
#define USB_MDNS                        1
#endif
#define LWIP_MDNS_RESPONDER             USB_MDNS   /* picoplug.local on USB; picoplug-N on the port */
#define MDNS_MAX_SERVICES               1          /* _picoplug._tcp on the port */
#define LWIP_MDNS_SEARCH                1          /* find the other testers */
#define MDNS_MAX_REQUESTS               1
#define MDNS_OUTPUT_PACKET_SIZE         1450
#define LWIP_NUM_NETIF_CLIENT_DATA      2      /* mDNS per-netif data, MLD6      */
#define LWIP_NETIF_EXT_STATUS_CALLBACK  1      /* mDNS watches address changes   */
#define LWIP_ICMP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_IPV4                       1
#define LWIP_IPV6                       1      /* link-local on the port only    */
#define LWIP_IPV6_AUTOCONFIG            0      /* no addresses from router adverts */
#define LWIP_IPV6_SEND_ROUTER_SOLICIT   0      /* and never ask for them         */
#define LWIP_IPV6_DHCP6                 0
#define LWIP_IPV6_MLD                   1      /* snooping switches forward ff02::fb */
#define LWIP_IPV6_FRAG                  0
#define LWIP_IPV6_REASS                 0
#define LWIP_ND6_NUM_NEIGHBORS          8
#define LWIP_ND6_NUM_DESTINATIONS       8
#define LWIP_ND6_NUM_PREFIXES           2
#define LWIP_ND6_NUM_ROUTERS            2
#define MEMP_NUM_MLD6_GROUP             6
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define ETHARP_SUPPORT_STATIC_ENTRIES   1
#define LWIP_SINGLE_NETIF               0      /* USB netif + Ethernet netif     */
#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_MULTICAST_PING             1
#define LWIP_BROADCAST_PING             1
#define ETH_PAD_SIZE                    0

/* Accept UDP to port 67 regardless of destination address (DHCP DISCOVER
 * from a client that has no address yet is sent to 255.255.255.255). */
#define LWIP_IP_ACCEPT_UDP_PORT(p)      ((p) == PP_NTOHS(67))
#define IP_SOF_BROADCAST                1
#define IP_SOF_BROADCAST_RECV           1

#define TCP_MSS                         (1500 - 20 - 20)
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_QUEUE_OOSEQ                 0
#define LWIP_TCP_KEEPALIVE              1

#define LWIP_STATS                      0
#define LWIP_CHECKSUM_CTRL_PER_NETIF    0

#ifndef NDEBUG
#define LWIP_DEBUG                      0
#endif

#endif /* LWIPOPTS_H */
