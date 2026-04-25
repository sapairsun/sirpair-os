#ifndef NET_H
#define NET_H

#include "types.h"

struct net_stats {
  uint tx_pkts;
  uint rx_pkts;
  uint tx_errs;
  uint rx_drop;
  uint rx_arp;
  uint rx_ipv4;
  uint rx_tcp;
  uint rx_udp;
  uint arp_reply;
  uint tcp_synack;
  uint tcp_ack;
  uint dhcp_offer;
  uint dhcp_ack;
};

struct net_cfg {
  uchar ip[4];
  uchar gw[4];
  uchar mask[4];
  uchar dns[4];
  uint lease_sec;
  int dhcp_ok;
  int dhcp_stage;
  int dhcp_last_err;
  uint dhcp_xid;
  uint dhcp_attempts;
  uint dhcp_offer_seen;
  uint dhcp_ack_seen;
  uint dhcp_discover_tx;
  uint dhcp_request_tx;
  uchar dhcp_offer_ip[4];
  uchar dhcp_server_ip[4];
  uchar dhcp_last_src_ip[4];
  int link_up;
  uint link_status;
  uint tx_pkts;
  uint rx_pkts;
  uint rx_udp;
  uchar mac[6];
  /* microps loopback (127.0.0.1/8); valid after microps core init */
  int lo_up;
  uchar lo_ip[4];
  uchar lo_mask[4];
};

void netinit(void);
int  net_is_available(void);
int  net_get_irq(void);
void net_get_mac(uchar mac[6]);
void net_get_ipv4(uchar ip[4]);
void net_get_dhcp_cfg(uchar gw[4], uchar mask[4], uchar dns[4], uint *lease_sec, int *ok);
void net_get_cfg(struct net_cfg *cfg);
void net_get_stats(struct net_stats *st);
int  net_tx_raw(void *data, int len);
void net_intr(void);
void net_poll(void);
int  net_dhcp_acquire(void);
void net_set_microps_l3(int enable);
int  net_get_microps_l3(void);
int  net_ping_interrupted(void);
int  net_ping(uint dst_ip, int count);
int  net_dns_query(const char *name, uint *out_ip);

#endif
