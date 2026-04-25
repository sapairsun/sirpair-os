#include "types.h"
#include "user.h"
#include "net.h"

#define IFNAME "eth0"
#define MTU 1500
#define LO_MTU 65536

/* Same flag bits as Linux uapi/linux/if.h */
#define IFF_UP          0x1u
#define IFF_BROADCAST   0x2u
#define IFF_RUNNING     0x40u
#define IFF_MULTICAST   0x1000u
#define IFF_LOOPBACK    0x8u

enum { IF_NONE = -1, IF_LO = 0, IF_ETH0 = 1 };

static int
streq(const char *a, const char *b)
{
  return strcmp(a, b) == 0;
}

static int
iface_by_name(const char *n)
{
  if(streq(n, "lo"))
    return IF_LO;
  if(streq(n, IFNAME))
    return IF_ETH0;
  return IF_NONE;
}

static int
is_ipv4_dotted(const char *s)
{
  int i, v, nd;
  const char *p = s;

  for(i = 0; i < 4; i++){
    v = 0;
    nd = 0;
    while(*p >= '0' && *p <= '9'){
      v = v * 10 + (*p - '0');
      if(v > 255)
        return 0;
      p++;
      nd++;
    }
    if(nd == 0)
      return 0;
    if(i < 3){
      if(*p != '.')
        return 0;
      p++;
    } else {
      if(*p != 0)
        return 0;
    }
  }
  return 1;
}

static void
print_ip(int fd, const uchar ip[4])
{
  printf(fd, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

static void
print_mac(int fd, const uchar m[6])
{
  int i;

  for(i = 0; i < 6; i++){
    printf(fd, "%s%02x", i ? ":" : "", m[i]);
  }
}

static void
bcast_addr(const uchar ip[4], const uchar mask[4], uchar out[4])
{
  int i;

  for(i = 0; i < 4; i++)
    out[i] = (uchar)((ip[i] & mask[i]) | ((uchar)~mask[i]));
}

static void
show_lo(int fd, struct net_cfg *cfg)
{
  uchar bc[4];
  uint flags;

  if(!cfg->lo_up)
    return;
  flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
  bcast_addr(cfg->lo_ip, cfg->lo_mask, bc);

  printf(fd, "lo: flags=%u<UP,LOOPBACK,RUNNING>  mtu %d\n",
         flags, LO_MTU);
  printf(fd, "        inet addr:");
  print_ip(fd, cfg->lo_ip);
  printf(fd, "  Mask:");
  print_ip(fd, cfg->lo_mask);
  printf(fd, "  Bcast:");
  print_ip(fd, bc);
  printf(fd, "\n");
  printf(fd, "        loop  txqueuelen 1000  (Local Loopback)\n");
  printf(fd, "        RX packets 0  TX packets 0  UDP rx (user) 0\n");
}

static void
show_eth0(int fd, struct net_cfg *cfg)
{
  uchar bc[4];
  uint flags;

  flags = IFF_UP | IFF_BROADCAST | IFF_MULTICAST;
  if(cfg->link_up)
    flags |= IFF_RUNNING;

  bcast_addr(cfg->ip, cfg->mask, bc);

  printf(fd, "%s: flags=%u<UP,BROADCAST%s,MULTICAST>  mtu %d\n",
         IFNAME, flags, cfg->link_up ? ",RUNNING" : "", MTU);
  printf(fd, "        inet addr:");
  print_ip(fd, cfg->ip);
  printf(fd, "  Mask:");
  print_ip(fd, cfg->mask);
  printf(fd, "  Bcast:");
  print_ip(fd, bc);
  printf(fd, "\n");
  printf(fd, "        ether ");
  print_mac(fd, cfg->mac);
  printf(fd, "  txqueuelen 1000  (Ethernet)\n");
  printf(fd, "        RX packets %u  TX packets %u  UDP rx (user) %u\n",
         cfg->rx_pkts, cfg->tx_pkts, cfg->rx_udp);
  if(cfg->dhcp_ok)
    printf(fd, "        DHCP: lease %u sec\n", cfg->lease_sec);
  else
    printf(fd, "        DHCP: none or static\n");
}

static int
config_args(int argc, char **argv, int start)
{
  int i;

  for(i = start; i < argc; i++){
    if(streq(argv[i], "up") || streq(argv[i], "down"))
      return 1;
    if(streq(argv[i], "netmask") || streq(argv[i], "broadcast") ||
       streq(argv[i], "mtu") || streq(argv[i], "add") ||
       streq(argv[i], "del") || streq(argv[i], "arp") ||
       streq(argv[i], "multicast"))
      return 1;
    if(is_ipv4_dotted(argv[i]))
      return 1;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  struct net_cfg cfg;
  int start;
  int which;

  start = 1;
  if(argc >= 2 && streq(argv[1], "-a")){
    start = 2;
  } else if(argc >= 2 && (streq(argv[1], "-h") || streq(argv[1], "--help"))){
    printf(1, "Usage: ifconfig [-a] [interface]\n");
    printf(1, "Show IPv4 addresses for lo (loopback) and %s (wired).\n", IFNAME);
    printf(1, "Runtime reconfiguration is not implemented (configure at boot).\n");
    exit(0);
  }

  if(getnetcfg(&cfg) < 0){
    printf(2, "ifconfig: cannot read network configuration\n");
    exit(1);
  }

  if(start >= argc){
    if(cfg.lo_up)
      show_lo(1, &cfg);
    show_eth0(1, &cfg);
    exit(0);
  }

  which = iface_by_name(argv[start]);
  if(which == IF_NONE){
    printf(2, "ifconfig: interface '%s' not found (only lo and %s)\n", argv[start],
           IFNAME);
    exit(1);
  }

  if(which == IF_LO && !cfg.lo_up){
    printf(2, "ifconfig: loopback is not available\n");
    exit(1);
  }

  if(config_args(argc, argv, start + 1)){
    printf(2, "ifconfig: changing address, netmask, broadcast, mtu, or up/down is not supported; use boot-time configuration.\n");
    exit(1);
  }

  if(start + 1 < argc){
    printf(2, "ifconfig: extra arguments\n");
    exit(1);
  }

  if(which == IF_LO)
    show_lo(1, &cfg);
  else
    show_eth0(1, &cfg);
  exit(0);
}
