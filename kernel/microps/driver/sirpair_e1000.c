#include "types.h"
#include "defs.h"
#include "net.h"
#include "microps_kernel.h"
#include "util.h"
#include "microps_net.h"
#include "ether.h"
#include "ip.h"

struct net_device *sirpair_eth_dev;

static ssize_t
sirpair_e1000_xmit_frame(struct net_device *dev, const uint8_t *frame, size_t flen)
{
  int r;

  (void)dev;
  if(flen > (size_t)1514)
    return -1;
  r = net_tx_raw((void *)frame, (int)flen);
  return r >= 0 ? (ssize_t)flen : -1;
}

static int
sirpair_e1000_transmit(struct net_device *dev, uint16_t type, const uint8_t *data,
                       size_t len, const void *dst)
{
  return ether_transmit_helper(dev, type, data, len, dst, sirpair_e1000_xmit_frame);
}

static struct net_device_ops sirpair_e1000_ops = {
  .transmit = sirpair_e1000_transmit,
};

static void
sirpair_e1000_setup(struct net_device *dev)
{
  uchar mac[6];

  ether_setup_helper(dev);
  net_get_mac(mac);
  memmove(dev->addr, mac, 6);
  dev->ops = &sirpair_e1000_ops;
}

int
sirpair_e1000_register_iface(const uchar ip[4], const uchar mask[4], const uchar gw[4])
{
  struct net_device *dev;
  struct ip_iface *iface;
  char ipstr[20];
  char maskstr[20];
  char gwstr[20];

  dev = net_device_alloc(sirpair_e1000_setup);
  if(!dev){
    errorf("net_device_alloc failed");
    return -1;
  }
  if(net_device_register(dev) < 0){
    errorf("net_device_register failed");
    return -1;
  }
  snprintf(ipstr, sizeof(ipstr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  snprintf(maskstr, sizeof(maskstr), "%d.%d.%d.%d", mask[0], mask[1], mask[2], mask[3]);
  iface = ip_iface_alloc(ipstr, maskstr);
  if(!iface){
    errorf("ip_iface_alloc failed");
    return -1;
  }
  if(ip_iface_register(dev, iface) < 0){
    errorf("ip_iface_register failed");
    return -1;
  }
  snprintf(gwstr, sizeof(gwstr), "%d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]);
  if(ip_route_set_default_gateway(iface, gwstr) < 0){
    errorf("ip_route_set_default_gateway failed");
    return -1;
  }
  sirpair_eth_dev = dev;
  return 0;
}

int
sirpair_e1000_sync_ndev_cfg(void)
{
  uchar ip[4];
  uchar mask[4];
  uchar gw[4];
  struct ip_iface *iface;
  char ips[20];
  char ms[20];
  char gwstr[20];

  net_get_ipv4(ip);
  net_get_dhcp_cfg(gw, mask, 0, 0, 0);
  snprintf(ips, sizeof(ips), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  snprintf(ms, sizeof(ms), "%d.%d.%d.%d", mask[0], mask[1], mask[2], mask[3]);
  snprintf(gwstr, sizeof(gwstr), "%d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]);

  if(!sirpair_eth_dev)
    return sirpair_e1000_register_iface(ip, mask, gw);

  iface = (struct ip_iface *)net_device_get_iface(sirpair_eth_dev, NET_IFACE_FAMILY_IP);
  if(!iface)
    return -1;
  return ip_iface_reconfigure(iface, ips, ms, gwstr);
}

void
sirpair_microps_deliver_ether(const uchar *frame, int len)
{
  struct ether_hdr {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
  } *eh;
  uint16_t etype;

  if(!sirpair_eth_dev){
    sirpair_eth_dev = net_device_first_ethernet();
    if(!sirpair_eth_dev){
      cprintf("mps eth drop: no dev len=%d\n", len);
      return;
    }
  }
  if(len < (int)sizeof(*eh))
    return;
  eh = (struct ether_hdr *)frame;
  etype = ntoh16(eh->type);
  if(memcmp(sirpair_eth_dev->addr, eh->dst, 6) != 0 &&
     memcmp(ETHER_ADDR_BROADCAST, eh->dst, 6) != 0){
    return;
  }
  net_input_handler(etype, (uint8_t *)(eh + 1), (size_t)len - sizeof(*eh), sirpair_eth_dev);
}
