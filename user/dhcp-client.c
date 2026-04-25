#include "types.h"
#include "user.h"
#include "net.h"

int
main(int argc, char **argv)
{
  struct net_cfg cfg;

  if(argc != 1){
    printf(2, "usage: dhcp-client\n");
    exit(0);
  }

  if(dhcp() < 0){
    if(getnetcfg(&cfg) == 0){
      printf(2, "dhcp-client: failed (stage=%d err=%d xid=%d try=%d offer=%d ack=%d)\n",
             cfg.dhcp_stage, cfg.dhcp_last_err, cfg.dhcp_xid, cfg.dhcp_attempts,
             cfg.dhcp_offer_seen, cfg.dhcp_ack_seen);
      printf(2, "dhcp-client: link=%d status=%d tx=%d rx=%d rxudp=%d discover_tx=%d request_tx=%d\n",
             cfg.link_up, cfg.link_status, cfg.tx_pkts, cfg.rx_pkts, cfg.rx_udp,
             cfg.dhcp_discover_tx, cfg.dhcp_request_tx);
      printf(2, "dhcp-client: last offer=%d.%d.%d.%d server=%d.%d.%d.%d src=%d.%d.%d.%d\n",
             cfg.dhcp_offer_ip[0], cfg.dhcp_offer_ip[1], cfg.dhcp_offer_ip[2], cfg.dhcp_offer_ip[3],
             cfg.dhcp_server_ip[0], cfg.dhcp_server_ip[1], cfg.dhcp_server_ip[2], cfg.dhcp_server_ip[3],
             cfg.dhcp_last_src_ip[0], cfg.dhcp_last_src_ip[1], cfg.dhcp_last_src_ip[2], cfg.dhcp_last_src_ip[3]);
    }
    printf(2, "dhcp-client: failed\n");
    exit(0);
  }

  printf(1, "dhcp-client: ok\n");
  if(getnetcfg(&cfg) < 0){
    printf(2, "dhcp-client: getnetcfg failed\n");
    exit(0);
  }
  printf(1, "IPv4: %d.%d.%d.%d\n", cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3]);
  printf(1, "MASK: %d.%d.%d.%d\n", cfg.mask[0], cfg.mask[1], cfg.mask[2], cfg.mask[3]);
  printf(1, "GW:   %d.%d.%d.%d\n", cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3]);
  printf(1, "DNS:  %d.%d.%d.%d\n", cfg.dns[0], cfg.dns[1], cfg.dns[2], cfg.dns[3]);
  printf(1, "LEASE: %d\n", cfg.lease_sec);
  printf(1, "DHCP-DIAG: stage=%d err=%d xid=%d try=%d offer=%d ack=%d\n",
         cfg.dhcp_stage, cfg.dhcp_last_err, cfg.dhcp_xid, cfg.dhcp_attempts,
         cfg.dhcp_offer_seen, cfg.dhcp_ack_seen);
  printf(1, "DHCP-LINK: link=%d status=%d tx=%d rx=%d rxudp=%d discover_tx=%d request_tx=%d\n",
         cfg.link_up, cfg.link_status, cfg.tx_pkts, cfg.rx_pkts, cfg.rx_udp,
         cfg.dhcp_discover_tx, cfg.dhcp_request_tx);
  printf(1, "DHCP-SRC: offer=%d.%d.%d.%d server=%d.%d.%d.%d src=%d.%d.%d.%d\n",
         cfg.dhcp_offer_ip[0], cfg.dhcp_offer_ip[1], cfg.dhcp_offer_ip[2], cfg.dhcp_offer_ip[3],
         cfg.dhcp_server_ip[0], cfg.dhcp_server_ip[1], cfg.dhcp_server_ip[2], cfg.dhcp_server_ip[3],
         cfg.dhcp_last_src_ip[0], cfg.dhcp_last_src_ip[1], cfg.dhcp_last_src_ip[2], cfg.dhcp_last_src_ip[3]);
  exit(0);
}
