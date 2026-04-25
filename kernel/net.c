#include "types.h"
#include "defs.h"
#include "x86.h"
#include "param.h"
#include "mmu.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "pci.h"
#include "fs.h"
#include "file.h"
#include "net.h"

extern void sirpair_microps_deliver_ether(const uchar *frame, int len);
extern int sirpair_microps_icmp_ping(uint dst_ip_u32, int count);
extern void sirpair_net_cfg_loopback(struct net_cfg *cfg);
extern int sirpair_e1000_sync_ndev_cfg(void);

#define NET_TX_RING 32
#define NET_RX_RING 32
#define NET_PKT_BUF 2048
#define NET_MTU     1500
#define NETIF_RXQ   32

/*
 * netif_process_rxq 运行在每进程 4096 字节内核栈上，已有 frame[NET_PKT_BUF]；
 * 勿在此处再叠放大块临时缓冲，以免内核栈溢出。
 */
#define DHCP_CLIENT_PORT 68
#define SSDP_PORT 1900
#define DHCP_SERVER_PORT 67
#define DHCP_MIN_PAYLOAD 300
#define DHCP_MAGIC 0x63825363U
#define DHCP_XID_BASE 0x4D435000U

// Intel e1000/e1000e 寄存器子集（与 Linux drivers/net/ethernet/intel/e1000* 布局一致的部分）。
#define E1000_CTRL   0x0000
#define E1000_STATUS 0x0008
#define E1000_CTRL_EXT 0x0018
#define E1000_ICR    0x00C0
#define E1000_IMC    0x00D8
#define E1000_RCTL   0x0100
#define E1000_RFCTL  0x05008 /* Receive Filter Control（Linux e1000e regs.h） */
#define E1000_TCTL   0x0400
#define E1000_TIPG   0x0410
#define E1000_RDBAL  0x2800
#define E1000_RDBAH  0x2804
#define E1000_RDLEN  0x2808
#define E1000_RDH    0x2810
#define E1000_RDT    0x2818
#define E1000_RXDCTL 0x2828
#define E1000_TDBAL  0x3800
#define E1000_TDBAH  0x3804
#define E1000_TDLEN  0x3808
#define E1000_TDH    0x3810
#define E1000_TDT    0x3818
#define E1000_TXDCTL 0x3828
#define E1000_RAL    0x5400
#define E1000_RAH    0x5404
#define E1000_MANC   0x5820
// Linux drivers/net/ethernet/intel/e1000e/ich8lan.h：82579 等与 ME 共享 CSR 时需尾指针写前仲裁（netdev.c __ew32_prepare）。
#define E1000_FWSM   0x05B54
#define E1000_ICH_FWSM_FW_VALID        0x00008000
#define E1000_ICH_FWSM_PCIM2PCI         0x01000000
#define E1000_ICH_FWSM_PCIM2PCI_COUNT   2000

#define E1000_CTRL_RST   (1 << 26)
#define E1000_CTRL_SLU   (1 << 6)
#define E1000_CTRL_EXT_DRV_LOAD (1U << 28)

#define E1000_RCTL_EN    (1 << 1)
#define E1000_RCTL_SBP   (1 << 2)
#define E1000_RCTL_UPE   (1 << 3)
#define E1000_RCTL_MPE   (1 << 4)
#define E1000_RCTL_LPE   (1 << 5)
#define E1000_RCTL_BAM   (1 << 15)
#define E1000_RCTL_SECRC (1 << 26)
#define E1000_RCTL_BSEX  (1U << 25)
#define E1000_RCTL_SZ_MASK (3U << 16)
#define E1000_RAH_AV     (1U << 31)
/* 接收过滤器扩展位：与 Linux e1000e_configure_rx 一致，e1000e 置位后写回为扩展描述符（wb.upper：
 * status_error 在偏移 8，length 在 12）；82540 仅 legacy 写回（len 在 8），须清除扩展位。 */
#define E1000_RFCTL_EXTEN 0x00008000U

#define E1000_TCTL_EN    (1 << 1)
#define E1000_TCTL_PSP   (1 << 3)
#define E1000_TCTL_CT    (0x10 << 4)
#define E1000_TCTL_COLD  (0x40 << 12)
#define E1000_TCTL_RTLC  (1U << 24)
#define E1000_DCTL_QUEUE_ENABLE (1U << 25)
#define E1000_DCTL_PTHRESH(x) ((x) & 0x3F)
#define E1000_DCTL_HTHRESH(x) (((x) & 0x3F) << 8)
#define E1000_DCTL_WTHRESH(x) (((x) & 0x3F) << 16)
// Linux e1000e defines.h：TXDCTL 字段掩码与 ICH/PCH 专用写回策略宏。
#define E1000_TXDCTL_PTHRESH_MASK 0x0000003FU
#define E1000_TXDCTL_WTHRESH_MASK 0x003F0000U
#define E1000_TXDCTL_FULL_TX_DESC_WB   0x01010000U
#define E1000_TXDCTL_MAX_TX_DESC_PREFETCH 0x0100001FU
// Linux e1000e：PCH/82579 等需 GRAN=1（按描述符粒度写回）；WTHRESH 过大曾导致发送停滞（见内核补丁 “Change wthresh to 1 to avoid possible Tx stalls”）。
#define E1000_TXDCTL_GRAN (1U << 24)
#define E1000_TXDCTL_COUNT_DESC 0x00400000U /* Linux defines.h：使能「待处理描述符计数」写回，82579 等与 FULL_TX_DESC_WB 同用 */
#define E1000_RXDCTL_GRAN (1U << 24)
// 第二发送/接收队列控制寄存器（双队列芯片上须与队列 0 一致，见 netdev.c “erratum: set txdctl the same for both queues”）。
#define E1000_TXDCTL_1 (0x3828 + 0x100)
#define E1000_RXDCTL_1 (0x2828 + 0x100)
// Linux ich8lan.c e1000_initialize_hw_bits_ich8lan：双队列发送仲裁（仅 82579/PCH 类需要；QEMU 82574 勿写）。
#define E1000_TARC0 0x03840
#define E1000_TARC1 0x03940
#define E1000_TCTL_MULR (1U << 28)
// 统计寄存器（只读清零），用于判断 MAC 是否实际计数到已发送帧。
#define E1000_GPTC 0x04080

#define E1000_TXD_CMD_EOP (1 << 0)
#define E1000_TXD_CMD_IFCS (1 << 1)
#define E1000_TXD_CMD_RS  (1 << 3)
#define E1000_TXD_STAT_DD (1 << 0)

#define E1000_RXD_STAT_DD  (1 << 0)
#define E1000_RXD_STAT_EOP (1 << 1)

#define E1000_STATUS_LU    (1 << 1)

#define E1000_MANC_RCV_TCO_EN          0x00020000U
#define E1000_MANC_EN_MAC_ADDR_FILTER  0x00100000U
#define E1000_MANC_EN_MNG2HOST         0x00200000U

#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_VLAN 0x8100
#define ETH_TYPE_VLAN_QINQ 0x88A8
#define ETH_TYPE_LLDP   0x88cc
#define ETH_TYPE_IPV6   0x86dd
/* 常见家用网关/交换机发送的二层类型（如环回或厂商诊断，载荷内可见 "loopback" 等），非 0x0800，与 DHCP 无关；静默丢弃。 */
#define ETH_TYPE_L2_VENDOR_LOOP 0xFFFA
#define ARP_HTYPE_ETH 1
#define ARP_PTYPE_IPV4 0x0800
#define ARP_OP_REQ 1
#define ARP_OP_REP 2
#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP 17
#define IP_PROTO_TCP 6
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQ 8

#define TCP_F_FIN 0x01
#define TCP_F_SYN 0x02
#define TCP_F_RST 0x04
#define TCP_F_PSH 0x08
#define TCP_F_ACK 0x10

struct txdesc {
  uint addr_lo;
  uint addr_hi;
  ushort len;
  uchar cso;
  uchar cmd;
  uchar status;
  uchar css;
  ushort special;
} __attribute__((packed));

struct rxdesc {
  uint addr_lo;
  uint addr_hi;
  union {
    struct {
      ushort len;
      ushort csum;
      uchar status;
      uchar errors;
      ushort special;
    } leg;
    struct {
      uint status_error;
      ushort length;
      ushort vlan;
    } ext;
  } u;
} __attribute__((packed));

struct eth_hdr {
  uchar dst[6];
  uchar src[6];
  ushort etype;
} __attribute__((packed));

struct arp_pkt {
  ushort htype;
  ushort ptype;
  uchar hlen;
  uchar plen;
  ushort op;
  uchar sha[6];
  uchar spa[4];
  uchar tha[6];
  uchar tpa[4];
} __attribute__((packed));

struct ip4_hdr {
  uchar vhl;
  uchar tos;
  ushort tot_len;
  ushort id;
  ushort frag_off;
  uchar ttl;
  uchar proto;
  ushort csum;
  uchar src[4];
  uchar dst[4];
} __attribute__((packed));

struct tcp_hdr {
  ushort sport;
  ushort dport;
  uint seq;
  uint ack;
  uchar offres;
  uchar flags;
  ushort win;
  ushort csum;
  ushort urg;
} __attribute__((packed));

struct udp_hdr {
  ushort sport;
  ushort dport;
  ushort len;
  ushort csum;
} __attribute__((packed));

struct icmp_echo {
  uchar type;
  uchar code;
  ushort csum;
  ushort ident;
  ushort seq;
} __attribute__((packed));

struct dhcp_pkt {
  uchar op;
  uchar htype;
  uchar hlen;
  uchar hops;
  uint xid;
  ushort secs;
  ushort flags;
  uchar ciaddr[4];
  uchar yiaddr[4];
  uchar siaddr[4];
  uchar giaddr[4];
  uchar chaddr[16];
  uchar sname[64];
  uchar file[128];
  uint magic;
  uchar opts[312];
} __attribute__((packed));

// Intel e1000/e1000e 要求 TDBAL/RDBAL 指向的描述符环至少 128 字节对齐（见 Linux e1000e 对 ich8lan/82579 的约束）。
// 描述符不能嵌在大结构体中间，否则 V2P(&ndev.tx) 往往不满足对齐，真机上会出现 TDH 不推进、DD 永不置位、发送环塞死。
static struct txdesc net_tx_ring[NET_TX_RING] __attribute__((aligned(128)));
static struct rxdesc net_rx_ring[NET_RX_RING] __attribute__((aligned(128)));

static struct {
  struct spinlock lock;
  int initialized;
  uint bus, dev, func;
  ushort vendor, device;
  int irq;
  volatile uint *mmio;
  uchar mac[6];
  uchar ip[4];
  uchar gw[4];
  uchar mask[4];
  uchar dns[4];
  uint lease_sec;
  int dhcp_ok;
  uchar arp_peer_mac[6];
  uchar arp_peer_ip[4];
  int arp_valid;

  uchar txbuf[NET_TX_RING][NET_PKT_BUF] __attribute__((aligned(16)));
  uchar rxbuf[NET_RX_RING][NET_PKT_BUF] __attribute__((aligned(16)));
  uint tx_tail;
  uint tx_clean;
  uint rx_clean;

  uchar rxq[NETIF_RXQ][NET_PKT_BUF];
  ushort rxq_len[NETIF_RXQ];
  int rxq_head;
  int rxq_tail;
  int rxq_cnt;

  uint tcp_isn;
  uint dhcp_xid;
  int dhcp_stage;
  uchar dhcp_offer_ip[4];
  uchar dhcp_server_ip[4];
  uchar dhcp_last_src_ip[4];
  uint dhcp_attempts;
  uint dhcp_offer_seen;
  uint dhcp_ack_seen;
  int dhcp_last_err;
  uint dhcp_discover_tx;
  uint dhcp_request_tx;
  uint dhcp_dbg_until;
  int dhcp_dbg_budget;
  int io_trace_budget;
  uchar dhcp_hint_ip[4];
  uchar dhcp_hint_mac[6];
  int use_microps;
  struct net_stats stats;
  int rx_wb_ext;
} ndev;

static uchar ip_loopback[4] = {127, 0, 0, 1};

static inline ushort
htons(ushort v)
{
  return (ushort)((v << 8) | (v >> 8));
}

static inline ushort
ntohs(ushort v)
{
  return htons(v);
}

static inline uint
htonl(uint v)
{
  return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
         ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}

static inline uint
ntohl(uint v)
{
  return htonl(v);
}

static uint
nr(uint reg)
{
  return ndev.mmio[reg / 4];
}

static void
nw(uint reg, uint val)
{
  ndev.mmio[reg / 4] = val;
}

static void
net_write_tdt(uint idx)
{
  nw(E1000_TDT, idx);
}

static void
net_write_rdt(uint idx)
{
  nw(E1000_RDT, idx);
}

static int
mac_is_zero(uchar m[6])
{
  int i;
  for(i = 0; i < 6; i++){
    if(m[i])
      return 0;
  }
  return 1;
}

static int
mac_is_broadcast(uchar m[6])
{
  int i;
  for(i = 0; i < 6; i++){
    if(m[i] != 0xFF)
      return 0;
  }
  return 1;
}

static int
mac_is_multicast(uchar m[6])
{
  return (m[0] & 1) != 0;
}

static int
ip_is_zero(uchar ip[4])
{
  return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static int
ip_eq(uchar a[4], uchar b[4])
{
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int
ip_is_broadcast(uchar ip[4])
{
  uchar bcast[4];
  int i;
  if(ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255)
    return 1;
  for(i = 0; i < 4; i++)
    bcast[i] = (uchar)(ndev.ip[i] | ~ndev.mask[i]);
  return ip_eq(ip, bcast);
}

static int
ip_is_multicast(uchar ip[4])
{
  return (ip[0] & 0xF0) == 0xE0;
}

static ushort
csum16(void *data, int len)
{
  uchar *p = data;
  uint sum = 0;
  int i;
  for(i = 0; i + 1 < len; i += 2)
    sum += (p[i] << 8) | p[i + 1];
  if(len & 1)
    sum += (uint)p[len - 1] << 8;
  while(sum >> 16)
    sum = (sum & 0xFFFFu) + (sum >> 16);
  return (ushort)(~sum & 0xFFFFu);
}

static void
net_tx_clean(void)
{
  struct txdesc *d;
  while(ndev.tx_clean != ndev.tx_tail){
    d = &net_tx_ring[ndev.tx_clean];
    if(!(d->status & E1000_TXD_STAT_DD))
      break;
    d->status = 0;
    ndev.tx_clean = (ndev.tx_clean + 1) % NET_TX_RING;
  }
}

static int
net_dhcp_dbg_enabled(void)
{
  if(ndev.dhcp_dbg_budget <= 0)
    return 0;
  if(ndev.dhcp_dbg_until == 0)
    return 0;
  return (int)ticks < (int)ndev.dhcp_dbg_until;
}

static int
net_io_trace_take(void)
{
  if(!net_dhcp_dbg_enabled())
    return 0;
  if(ndev.io_trace_budget <= 0)
    return 0;
  ndev.io_trace_budget--;
  return 1;
}

static int
net_known_etype(ushort etype)
{
  return etype == ETH_TYPE_ARP || etype == ETH_TYPE_IPV4 ||
         etype == ETH_TYPE_VLAN || etype == ETH_TYPE_VLAN_QINQ;
}

static void
net_l2_try_8023_snap_ipv4(uchar *frame, int *len)
{
  struct eth_hdr *eh;
  ushort et;
  if(*len < 22)
    return;
  eh = (struct eth_hdr *)frame;
  et = ntohs(eh->etype);
  if(et > 0x05DC)
    return;
  if(frame[14] == 0xAA && frame[15] == 0xAA && frame[16] == 0x03 &&
     frame[17] == 0x00 && frame[18] == 0x00 && frame[19] == 0x00 &&
     frame[20] == 0x08 && frame[21] == 0x00){
    memmove(frame + 8, frame, 14);
    *len -= 8;
    eh = (struct eth_hdr *)frame;
    eh->etype = htons(ETH_TYPE_IPV4);
  }
}

static void
net_l2_align_ipv4(uchar *frame, int *len)
{
  (void)frame;
  (void)len;
}

static void
net_l2_debug_hint_ipv4_offset(uchar *frame, int len)
{
  (void)frame;
  (void)len;
}

static int
udp_hdr_ok(struct ip4_hdr *ip, int framelen, int ihl, struct udp_hdr **udp_out)
{
  int iptot;
  struct udp_hdr *udp;
  if(ip->proto != IP_PROTO_UDP)
    return 0;
  if(framelen < (int)(sizeof(struct eth_hdr) + ihl + (int)sizeof(struct udp_hdr)))
    return 0;
  iptot = ntohs(ip->tot_len);
  if(iptot < ihl + (int)sizeof(struct udp_hdr))
    return 0;
  udp = (struct udp_hdr *)((uchar *)ip + ihl);
  *udp_out = udp;
  return 1;
}

static int
udp_is_dhcp_to_client(struct ip4_hdr *ip, int framelen, int ihl)
{
  struct udp_hdr *udp;
  if(!udp_hdr_ok(ip, framelen, ihl, &udp))
    return 0;
  return ntohs(udp->sport) == DHCP_SERVER_PORT &&
         ntohs(udp->dport) == DHCP_CLIENT_PORT;
}

static int
udp_is_dhcp_to_server(struct ip4_hdr *ip, int framelen, int ihl)
{
  struct udp_hdr *udp;
  if(!udp_hdr_ok(ip, framelen, ihl, &udp))
    return 0;
  return ntohs(udp->sport) == DHCP_CLIENT_PORT &&
         ntohs(udp->dport) == DHCP_SERVER_PORT;
}

static int
udp_is_ssdp(struct ip4_hdr *ip, int framelen, int ihl)
{
  struct udp_hdr *udp;
  if(!udp_hdr_ok(ip, framelen, ihl, &udp))
    return 0;
  return ntohs(udp->dport) == SSDP_PORT;
}

static int
net_pch_pcim2pci_wa_needed(void)
{
  uint f = nr(E1000_FWSM);
  if((f & E1000_ICH_FWSM_FW_VALID) && (f & E1000_ICH_FWSM_PCIM2PCI))
    return 1;
  return 0;
}

static void
net_tx_soft_reset(void)
{
  uint tdh, tdt;

  if(!ndev.mmio)
    return;
  /*
   * DHCP 重入前必须回收已完成描述符，并使软件 tx_clean/tx_tail 与硬件 TDH/TDT 一致。
   * 旧实现曾清零整环并把 tail/clean 置 0 却不写回寄存器：若此前已发送多帧，硬件 TDT 已前进，
   * 后续 net_tx_raw 会「回卷」写 TDT，易使 e1000e 进入异常状态，典型现象为仅发不收（rx=0），
   * 用户态 dhcp-client 第二次申请时必现。
   */
  acquire(&ndev.lock);
  net_tx_clean();
  tdh = nr(E1000_TDH) & (NET_TX_RING - 1);
  tdt = nr(E1000_TDT) & (NET_TX_RING - 1);
  ndev.tx_clean = (int)tdh;
  ndev.tx_tail = (int)tdt;
  release(&ndev.lock);
}

static int
net_link_up(void)
{
  if(!ndev.mmio)
    return 0;
  return (nr(E1000_STATUS) & E1000_STATUS_LU) != 0;
}

static ushort
udp_checksum(struct ip4_hdr *ip, struct udp_hdr *udp, int udplen)
{
  uint sum = 0;
  int i;
  uchar *p = (uchar*)udp;

  sum += (ip->src[0] << 8) | ip->src[1];
  sum += (ip->src[2] << 8) | ip->src[3];
  sum += (ip->dst[0] << 8) | ip->dst[1];
  sum += (ip->dst[2] << 8) | ip->dst[3];
  sum += IP_PROTO_UDP;
  sum += udplen;

  for(i = 0; i + 1 < udplen; i += 2)
    sum += (p[i] << 8) | p[i + 1];
  if(udplen & 1)
    sum += p[udplen - 1] << 8;

  while(sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  sum = ~sum;
  if(sum == 0)
    sum = 0xFFFF;
  return (ushort)sum;
}

static int
is_supported_nic(ushort vendor, ushort device)
{
  if(vendor != 0x8086)
    return 0;
  // 0x100e: QEMU 旧 e1000(82540EM)；0x10d3: QEMU e1000e(82574L)；0x1502/1503: 82579LM 等真机。
  return (device == 0x100e || device == 0x1502 || device == 0x1503 || device == 0x10d3);
}

static int
net_is_e1000e(void)
{
  return ndev.device == 0x10d3 || ndev.device == 0x1502 || ndev.device == 0x1503;
}

static int
net_is_82579_lm(void)
{
  return ndev.device == 0x1502 || ndev.device == 0x1503;
}

// Linux ich8lan.c e1000_initialize_hw_bits_ich8lan：PCH 上补 TXDCTL 的 COUNT_DESC(BIT22)，
// 并配置 TARC0/TARC1，避免 82579LM 上发送描述符 DD 写回异常、环假满（与 dhcp 失败时 discover>>tx 对照）。
static void
net_init_82579_tx_extra(void)
{
  uint v, tarc;

  if(!net_is_82579_lm())
    return;
  v = nr(E1000_TXDCTL);
  v |= E1000_TXDCTL_COUNT_DESC;
  nw(E1000_TXDCTL, v);
  nw(E1000_TXDCTL_1, v);
  tarc = nr(E1000_TARC0);
  tarc |= (1U << 23) | (1U << 24) | (1U << 26) | (1U << 27);
  nw(E1000_TARC0, tarc);
  tarc = nr(E1000_TARC1);
  if(nr(E1000_TCTL) & E1000_TCTL_MULR)
    tarc &= ~(1U << 28);
  else
    tarc |= (1U << 28);
  tarc |= (1U << 24) | (1U << 26) | (1U << 30);
  nw(E1000_TARC1, tarc);
  cprintf("net: 82579 pch txdctl=%x tarc0=%x tarc1=%x",
          nr(E1000_TXDCTL), nr(E1000_TARC0), nr(E1000_TARC1));
  boot_ok();
}

static uint
nic_read_bar_mem(uint bus, uint dev, uint func)
{
  uint bar0 = pci_read32(bus, dev, func, PCI_BAR0);
  uint bar1 = pci_read32(bus, dev, func, PCI_BAR1);
  if((bar0 & 1) == 0 && (bar0 & ~0xF))
    return bar0 & ~0xF;
  if((bar1 & 1) == 0 && (bar1 & ~0xF))
    return bar1 & ~0xF;
  return 0;
}

static void
net_read_mac(void)
{
  uint ral = nr(E1000_RAL);
  uint rah = nr(E1000_RAH);
  ndev.mac[0] = ral & 0xFF;
  ndev.mac[1] = (ral >> 8) & 0xFF;
  ndev.mac[2] = (ral >> 16) & 0xFF;
  ndev.mac[3] = (ral >> 24) & 0xFF;
  ndev.mac[4] = rah & 0xFF;
  ndev.mac[5] = (rah >> 8) & 0xFF;
  if(mac_is_zero(ndev.mac) || mac_is_broadcast(ndev.mac) || mac_is_multicast(ndev.mac)){
    cprintf("net: warn suspicious mac from RAL/RAH ral=%x rah=%x mac=%x:%x:%x:%x:%x:%x",
            ral, rah,
            ndev.mac[0], ndev.mac[1], ndev.mac[2], ndev.mac[3], ndev.mac[4], ndev.mac[5]);
    boot_fail();
  } else {
    cprintf("net: mac regs ral=%x rah=%x mac=%x:%x:%x:%x:%x:%x",
            ral, rah,
            ndev.mac[0], ndev.mac[1], ndev.mac[2], ndev.mac[3], ndev.mac[4], ndev.mac[5]);
    boot_ok();
  }
}

static void
net_program_mac_filter(void)
{
  uint ral, rah;
  ral = (uint)ndev.mac[0] |
        ((uint)ndev.mac[1] << 8) |
        ((uint)ndev.mac[2] << 16) |
        ((uint)ndev.mac[3] << 24);
  rah = (uint)ndev.mac[4] |
        ((uint)ndev.mac[5] << 8) |
        E1000_RAH_AV;
  nw(E1000_RAL, ral);
  nw(E1000_RAH, rah);
}

static void
net_hw_reset(void)
{
  int i;
  uint manc_before, manc_after;
  nw(E1000_CTRL, nr(E1000_CTRL) | E1000_CTRL_RST);
  for(i = 0; i < 1000000; i++)
    ;
  nw(E1000_IMC, 0xFFFFFFFF);
  nr(E1000_ICR);
  // Only keep SLU forced on QEMU e1000. On X220/82579LM, forcing SLU can
  // interfere with real PHY autonegotiation and create false link-up.
  if(ndev.device == 0x100e)
    nw(E1000_CTRL, nr(E1000_CTRL) | E1000_CTRL_SLU);
  else
    nw(E1000_CTRL, nr(E1000_CTRL) & ~E1000_CTRL_SLU);
  // Tell firmware/ME the host driver is active (important on 82579LM/e1000e).
  nw(E1000_CTRL_EXT, nr(E1000_CTRL_EXT) | E1000_CTRL_EXT_DRV_LOAD);
  // X220/82579LM: prefer "management-to-host allowed" while disabling stricter
  // management filters that may hide DHCP/ARP from host stack.
  manc_before = nr(E1000_MANC);
  manc_after = manc_before;
  manc_after &= ~E1000_MANC_RCV_TCO_EN;
  manc_after &= ~E1000_MANC_EN_MAC_ADDR_FILTER;
  manc_after |= E1000_MANC_EN_MNG2HOST;
  nw(E1000_MANC, manc_after);
  cprintf("net: manc before=%x after=%x", manc_before, nr(E1000_MANC));
  boot_ok();
}

static void
net_init_tx(void)
{
  int i;
  uint v;
  int t;
  // Linux e1000e_setup_tx_resources：发送描述符初始为 0；切勿预置 DD。若未用槽位带 DD，
  // net_tx_clean 会误回收，tx_clean 会“追上”未真正发出的描述符，最终在真机上表现为环假满、计数异常。
  for(i = 0; i < NET_TX_RING; i++){
    net_tx_ring[i].addr_lo = V2P(ndev.txbuf[i]);
    net_tx_ring[i].addr_hi = 0;
    net_tx_ring[i].len = 0;
    net_tx_ring[i].cso = 0;
    net_tx_ring[i].cmd = 0;
    net_tx_ring[i].status = 0;
    net_tx_ring[i].css = 0;
    net_tx_ring[i].special = 0;
  }
  ndev.tx_tail = 0;
  ndev.tx_clean = 0;
  clflush_range(net_tx_ring, sizeof(net_tx_ring));
  mfence();
  nw(E1000_TDBAL, V2P(net_tx_ring));
  nw(E1000_TDBAH, 0);
  nw(E1000_TDLEN, sizeof(net_tx_ring));
  nw(E1000_TDH, 0);
  net_write_tdt(0);
  // Linux ich8lan.c e1000_reset_hw_ich8lan：对 ICH/PCH 双队列设置“发送描述符写回策略”，并令 TXDCTL(1)=TXDCTL(0)（netdev.c 双队列 erratum）。
  // 对 e1000e 系（含 QEMU 82574 与真机 82579）采用 FULL_TX_DESC_WB + MAX_TX_DESC_PREFETCH，避免 DD 长期不置位导致环假满。
  if(net_is_e1000e()){
    v = nr(E1000_TXDCTL);
    v = (v & ~E1000_TXDCTL_WTHRESH_MASK) | E1000_TXDCTL_FULL_TX_DESC_WB;
    v = (v & ~E1000_TXDCTL_PTHRESH_MASK) | E1000_TXDCTL_MAX_TX_DESC_PREFETCH;
    v |= E1000_DCTL_QUEUE_ENABLE;
    nw(E1000_TXDCTL, v);
    nw(E1000_TXDCTL_1, v);
  } else {
    v = E1000_DCTL_PTHRESH(8) | E1000_DCTL_HTHRESH(1) | E1000_DCTL_WTHRESH(4);
    v |= E1000_DCTL_QUEUE_ENABLE;
    nw(E1000_TXDCTL, v);
  }
  for(t = 0; t < 100000; t++){
    if(nr(E1000_TXDCTL) & E1000_DCTL_QUEUE_ENABLE)
      break;
  }
  nw(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_RTLC | E1000_TCTL_CT | E1000_TCTL_COLD);
  nw(E1000_TIPG, 10 | (8 << 10) | (6 << 20));
  net_init_82579_tx_extra();
  cprintf("net: txq txdctl=%x tctl=%x tipg=%x", nr(E1000_TXDCTL), nr(E1000_TCTL), nr(E1000_TIPG));
  boot_ok();
}

static void
net_init_rx(void)
{
  int i;
  uint v;
  int t;
  if(net_is_e1000e()){
    v = nr(E1000_RFCTL);
    v |= E1000_RFCTL_EXTEN;
    nw(E1000_RFCTL, v);
    ndev.rx_wb_ext = 1;
  } else {
    v = nr(E1000_RFCTL);
    v &= ~E1000_RFCTL_EXTEN;
    nw(E1000_RFCTL, v);
    ndev.rx_wb_ext = 0;
  }
  for(i = 0; i < NET_RX_RING; i++){
    net_rx_ring[i].addr_lo = V2P(ndev.rxbuf[i]);
    net_rx_ring[i].addr_hi = 0;
    memset(&net_rx_ring[i].u, 0, sizeof(net_rx_ring[i].u));
  }
  ndev.rxq_head = ndev.rxq_tail = ndev.rxq_cnt = 0;
  nw(E1000_RDBAL, V2P(net_rx_ring));
  nw(E1000_RDBAH, 0);
  nw(E1000_RDLEN, sizeof(net_rx_ring));
  nw(E1000_RDH, 0);
  net_write_rdt(NET_RX_RING - 1);
  if(net_is_e1000e()){
    v = E1000_DCTL_QUEUE_ENABLE | E1000_RXDCTL_GRAN |
        E1000_DCTL_PTHRESH(8) | E1000_DCTL_HTHRESH(8) | E1000_DCTL_WTHRESH(1);
    nw(E1000_RXDCTL, v);
    nw(E1000_RXDCTL_1, v);
  } else {
    v = E1000_DCTL_PTHRESH(8) | E1000_DCTL_HTHRESH(8) | E1000_DCTL_WTHRESH(4);
    v |= E1000_DCTL_QUEUE_ENABLE;
    nw(E1000_RXDCTL, v);
  }
  for(t = 0; t < 100000; t++){
    if(nr(E1000_RXDCTL) & E1000_DCTL_QUEUE_ENABLE)
      break;
  }
  net_program_mac_filter();
  // 与 Linux e1000e 一致：不设 SBP；显式设接收缓冲 2048 并清 BSEX，避免复位后 BSIZE 与 NET_PKT_BUF 不一致导致分片描述符丢包。
  // UPE/MPE 便于捕获广播/多播 DHCP。
  {
    uint rctl;
    rctl = E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_LPE | E1000_RCTL_BAM | E1000_RCTL_SECRC;
    rctl &= ~(E1000_RCTL_BSEX | E1000_RCTL_SZ_MASK);
    nw(E1000_RCTL, rctl);
  }
  ndev.rx_clean = 0;
  cprintf("net: rxq rxdctl=%x rctl=%x rfctl=%x rx_ext=%d",
          nr(E1000_RXDCTL), nr(E1000_RCTL), nr(E1000_RFCTL), ndev.rx_wb_ext);
  boot_ok();
}

static int
net_probe_pci(void)
{
  uint bus, dev, func;
  ushort vendor, device;
  uint classrev;
  uchar class, subclass;
  uint bar;

  for(bus = 0; bus < 256; bus++){
    for(dev = 0; dev < 32; dev++){
      for(func = 0; func < 8; func++){
        vendor = pci_read16(bus, dev, func, PCI_VENDOR_ID);
        if(vendor == 0xFFFF)
          continue;
        device = pci_read16(bus, dev, func, PCI_DEVICE_ID);
        classrev = pci_read32(bus, dev, func, PCI_CLASS_REV);
        class = (classrev >> 24) & 0xFF;
        subclass = (classrev >> 16) & 0xFF;
        if(class != 0x02 || subclass != 0x00)
          continue;
        if(!is_supported_nic(vendor, device))
          continue;
        bar = nic_read_bar_mem(bus, dev, func);
        if(bar < DEVSPACE)
          continue;
        ndev.bus = bus;
        ndev.dev = dev;
        ndev.func = func;
        ndev.vendor = vendor;
        ndev.device = device;
        ndev.irq = pci_read32(bus, dev, func, PCI_INTERRUPT) & 0xFF;
        ndev.mmio = (volatile uint*)bar;
        return 0;
      }
    }
  }
  return -1;
}

int
net_tx_raw(void *data, int len)
{
  struct txdesc *d;
  uint idx;
  uint next;
  int w;
  int i;

  if(!ndev.initialized || data == 0 || len <= 0 || len > NET_PKT_BUF)
    return -1;
  /* IEEE 802.3：线路上最小帧 60 字节（不含 FCS）；过短帧可能被交换机或 slirp 丢弃 */
  if(len < 60 && len < NET_PKT_BUF){
    memset((uchar*)data + len, 0, (uint)(60 - len));
    len = 60;
  }

  acquire(&ndev.lock);
  for(w = 0; w < 64; w++){
    net_tx_clean();
    idx = ndev.tx_tail;
    next = (idx + 1) % NET_TX_RING;
    if(next != ndev.tx_clean)
      break;
    for(i = 0; i < 8000; i++)
      ;
  }
  d = &net_tx_ring[idx];
  if(next == ndev.tx_clean){
    ndev.stats.tx_errs++;
    if(net_dhcp_dbg_enabled()){
      cprintf("net: tx full idx=%d next=%d clean=%d tdh=%d tdt=%d dd@%d=%x status=%x gptc=%x\n",
              idx, next, ndev.tx_clean, nr(E1000_TDH), nr(E1000_TDT),
              ndev.tx_clean, net_tx_ring[ndev.tx_clean].status,
              nr(E1000_STATUS), nr(E1000_GPTC));
      ndev.dhcp_dbg_budget--;
    }
    release(&ndev.lock);
    return -1;
  }
  memmove(ndev.txbuf[idx], data, len);
  d->len = len;
  d->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
  d->status = 0;
  clflush_range(ndev.txbuf[idx], (uint)len);
  clflush_range(d, sizeof(*d));
  mfence();
  // 82579LM（8086:1502）等：部分机型上发送描述符与帧缓冲虽已按行刷新，PCI 主控仍可能读到旧行；与 Linux 对非一致性映射使用 dma_sync 类似，此处用整缓存写回兜底。
  if(net_is_e1000e())
    wbinvd();
  ndev.tx_tail = next;
  net_write_tdt(ndev.tx_tail);
  ndev.stats.tx_pkts++;
  if(net_io_trace_take()){
    cprintf("net: io tx len=%d ring=%d tdh=%x tdt=%x pci=%x:%x\n",
            len, idx, nr(E1000_TDH), nr(E1000_TDT), ndev.vendor, ndev.device);
  }
  release(&ndev.lock);
  return len;
}

static int
net_send_arp_reply(uchar dst_mac[6], uchar dst_ip[4])
{
  uchar pkt[64];
  struct eth_hdr *eh = (struct eth_hdr*)pkt;
  struct arp_pkt *ap = (struct arp_pkt*)(pkt + sizeof(*eh));
  memset(pkt, 0, sizeof(pkt));
  memmove(eh->dst, dst_mac, 6);
  memmove(eh->src, ndev.mac, 6);
  eh->etype = htons(ETH_TYPE_ARP);
  ap->htype = htons(ARP_HTYPE_ETH);
  ap->ptype = htons(ARP_PTYPE_IPV4);
  ap->hlen = 6;
  ap->plen = 4;
  ap->op = htons(ARP_OP_REP);
  memmove(ap->sha, ndev.mac, 6);
  memmove(ap->spa, ndev.ip, 4);
  memmove(ap->tha, dst_mac, 6);
  memmove(ap->tpa, dst_ip, 4);
  ndev.stats.arp_reply++;
  return net_tx_raw(pkt, sizeof(struct eth_hdr) + sizeof(struct arp_pkt));
}

static int
net_send_icmp_pkt(uchar dst_mac[6], uchar src_ip[4], uchar dst_ip[4],
                  uchar type, ushort ident, ushort seq,
                  uchar *payload, int payload_len)
{
  uchar pkt[NET_PKT_BUF];
  struct eth_hdr *eh = (struct eth_hdr*)pkt;
  struct ip4_hdr *ip = (struct ip4_hdr*)(pkt + sizeof(*eh));
  struct icmp_echo *icmp = (struct icmp_echo*)((uchar*)ip + sizeof(*ip));
  int icmp_len = sizeof(*icmp) + payload_len;
  int ip_len = sizeof(*ip) + icmp_len;
  int frame_len = sizeof(*eh) + ip_len;

  if(payload_len < 0 || payload_len > NET_MTU || frame_len > NET_PKT_BUF)
    return -1;

  memset(pkt, 0, frame_len);
  memmove(eh->dst, dst_mac, 6);
  memmove(eh->src, ndev.mac, 6);
  eh->etype = htons(ETH_TYPE_IPV4);

  ip->vhl = 0x45;
  ip->tot_len = htons(ip_len);
  ip->id = htons((ushort)(ndev.tcp_isn & 0xFFFF));
  ip->frag_off = htons(0x4000);
  ip->ttl = 64;
  ip->proto = IP_PROTO_ICMP;
  memmove(ip->src, src_ip, 4);
  memmove(ip->dst, dst_ip, 4);
  ip->csum = 0;
  ip->csum = htons(csum16(ip, sizeof(*ip)));

  icmp->type = type;
  icmp->code = 0;
  icmp->ident = htons(ident);
  icmp->seq = htons(seq);
  if(payload_len > 0)
    memmove((uchar*)icmp + sizeof(*icmp), payload, payload_len);
  icmp->csum = 0;
  icmp->csum = htons(csum16(icmp, icmp_len));
  return net_tx_raw(pkt, frame_len);
}

static int
net_send_udp_pkt_opts(uchar dst_mac[6], uchar src_ip[4], uchar dst_ip[4],
                      ushort sport, ushort dport, void *payload, int plen, int with_csum)
{
  uchar pkt[NET_PKT_BUF];
  struct eth_hdr *eh = (struct eth_hdr*)pkt;
  struct ip4_hdr *ip = (struct ip4_hdr*)(pkt + sizeof(*eh));
  struct udp_hdr *udp = (struct udp_hdr*)((uchar*)ip + sizeof(*ip));
  int iplen = sizeof(*ip) + sizeof(*udp) + plen;
  int framelen = sizeof(*eh) + iplen;

  if(plen < 0 || plen > NET_MTU || framelen > NET_PKT_BUF)
    return -1;

  memset(pkt, 0, framelen);
  memmove(eh->dst, dst_mac, 6);
  memmove(eh->src, ndev.mac, 6);
  eh->etype = htons(ETH_TYPE_IPV4);

  ip->vhl = 0x45;
  ip->tot_len = htons(iplen);
  ip->id = htons((ushort)(ndev.tcp_isn & 0xFFFF));
  ip->frag_off = htons(0x4000);
  ip->ttl = 64;
  ip->proto = IP_PROTO_UDP;
  memmove(ip->src, src_ip, 4);
  memmove(ip->dst, dst_ip, 4);
  ip->csum = 0;
  ip->csum = htons(csum16(ip, sizeof(*ip)));

  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->len = htons(sizeof(*udp) + plen);
  udp->csum = 0; // default keeps broad DHCP compatibility

  if(plen > 0)
    memmove((uchar*)udp + sizeof(*udp), payload, plen);
  if(with_csum)
    udp->csum = htons(udp_checksum(ip, udp, sizeof(*udp) + plen));
  return net_tx_raw(pkt, framelen);
}

static int
net_send_udp_pkt(uchar dst_mac[6], uchar src_ip[4], uchar dst_ip[4],
                 ushort sport, ushort dport, void *payload, int plen)
{
  return net_send_udp_pkt_opts(dst_mac, src_ip, dst_ip, sport, dport, payload, plen, 0);
}

static int
net_send_udp_pkt_csum(uchar dst_mac[6], uchar src_ip[4], uchar dst_ip[4],
                      ushort sport, ushort dport, void *payload, int plen)
{
  return net_send_udp_pkt_opts(dst_mac, src_ip, dst_ip, sport, dport, payload, plen, 1);
}

static int
net_dhcp_send_to(uchar dst_mac[6], uchar dst_ip[4], int msgtype,
                 uchar reqip[4], uchar server[4])
{
  uchar src_ip[4] = {0,0,0,0};
  struct dhcp_pkt d;
  uchar *o;
  int plen;

  memset(&d, 0, sizeof(d));
  d.op = 1;
  d.htype = 1;
  d.hlen = 6;
  d.xid = htonl(ndev.dhcp_xid);
  d.flags = htons(0x8000); // broadcast
  memmove(d.chaddr, ndev.mac, 6);
  d.magic = htonl(DHCP_MAGIC);

  o = d.opts;
  *o++ = 53; *o++ = 1; *o++ = (uchar)msgtype;
  // Client identifier (type 1 + MAC) improves compatibility with strict servers.
  *o++ = 61; *o++ = 7; *o++ = 1;
  memmove(o, ndev.mac, 6); o += 6;
  // Parameter request list: mask/router/dns/domain/lease/server/renew/rebind.
  *o++ = 55; *o++ = 8;
  *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 15;
  *o++ = 51; *o++ = 54; *o++ = 58; *o++ = 59;
  // Maximum DHCP message size.
  *o++ = 57; *o++ = 2; *o++ = 0x05; *o++ = 0xDC; // 1500
  if(msgtype == 3 && reqip && server){
    *o++ = 50; *o++ = 4;
    memmove(o, reqip, 4); o += 4;
    *o++ = 54; *o++ = 4;
    memmove(o, server, 4); o += 4;
  }
  *o++ = 255;
  plen = (int)((uchar*)o - (uchar*)&d);
  if(plen < DHCP_MIN_PAYLOAD)
    plen = DHCP_MIN_PAYLOAD;
  // 兼容性：仅对 QEMU 82540EM(0x100e) 再发一份带校验的副本；e1000e/82579LM 走单包路径。
  if(net_send_udp_pkt(dst_mac, src_ip, dst_ip, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &d, plen) < 0){
    if(net_dhcp_dbg_enabled()){
      cprintf("dhcp: tx raw failed type=%d xid=%x\n", msgtype, ndev.dhcp_xid);
      ndev.dhcp_dbg_budget--;
    }
    return -1;
  }
  if(msgtype == 1)
    ndev.dhcp_discover_tx++;
  else if(msgtype == 3)
    ndev.dhcp_request_tx++;
  if(net_dhcp_dbg_enabled()){
    cprintf("dhcp: tx type=%d xid=%x stage=%d discover=%d request=%d\n",
            msgtype, ndev.dhcp_xid, ndev.dhcp_stage,
            ndev.dhcp_discover_tx, ndev.dhcp_request_tx);
    cprintf("dhcp: tx l2 %x:%x:%x:%x:%x:%x -> %x:%x:%x:%x:%x:%x l3 %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d\n",
            ndev.mac[0], ndev.mac[1], ndev.mac[2], ndev.mac[3], ndev.mac[4], ndev.mac[5],
            dst_mac[0], dst_mac[1], dst_mac[2], dst_mac[3], dst_mac[4], dst_mac[5],
            src_ip[0], src_ip[1], src_ip[2], src_ip[3], DHCP_CLIENT_PORT,
            dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], DHCP_SERVER_PORT);
    ndev.dhcp_dbg_budget--;
  }
  if(ndev.device == 0x100e){
    if(net_send_udp_pkt_csum(dst_mac, src_ip, dst_ip, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &d, plen) < 0 &&
       net_dhcp_dbg_enabled()){
      cprintf("dhcp: tx csum failed type=%d xid=%x\n", msgtype, ndev.dhcp_xid);
      ndev.dhcp_dbg_budget--;
    }
  }
  return plen;
}

static int
net_dhcp_send(int msgtype, uchar reqip[4], uchar server[4])
{
  uchar bcast_mac[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
  uchar bcast_ip[4] = {255,255,255,255};
  return net_dhcp_send_to(bcast_mac, bcast_ip, msgtype, reqip, server);
}

static void
net_handle_dhcp(struct dhcp_pkt *d, int dlen)
{
  uchar *o, *end;
  int mtype = 0;
  uchar server[4] = {0,0,0,0};
  uchar mask[4] = {255,255,255,0};
  uchar gw[4] = {0,0,0,0};
  uchar dns[4] = {0,0,0,0};
  uint lease = 0;

  if(dlen < (int)(sizeof(struct dhcp_pkt) - sizeof(d->opts)))
    return;
  if(d->op != 2 || d->htype != 1 || d->hlen != 6)
    return;
  if(ntohl(d->xid) != ndev.dhcp_xid)
    return;
  if(d->magic != htonl(DHCP_MAGIC))
    return;
  {
    int i, nz = 0;
    for(i = 0; i < 6; i++){
      if(d->chaddr[i])
        nz = 1;
    }
    if(nz && memcmp(d->chaddr, ndev.mac, 6) != 0)
      return;
  }

  o = d->opts;
  end = ((uchar*)d) + dlen;
  while(o < end){
    uchar code = *o++;
    uchar olen;
    if(code == 0)
      continue;
    if(code == 255)
      break;
    if(o >= end)
      break;
    olen = *o++;
    if(o + olen > end)
      break;
    if(code == 53 && olen >= 1)
      mtype = o[0];
    else if(code == 54 && olen >= 4)
      memmove(server, o, 4);
    else if(code == 1 && olen >= 4)
      memmove(mask, o, 4);
    else if(code == 3 && olen >= 4)
      memmove(gw, o, 4);
    else if(code == 6 && olen >= 4)
      memmove(dns, o, 4);
    else if(code == 51 && olen >= 4)
      lease = ntohl(*(uint*)o);
    o += olen;
  }

  // 少数家用路由的应答带 yiaddr/siaddr，但选项 53 缺失或落在截断之后；与常见用户态栈一致，按 yiaddr 推断为「提供」。
  if(mtype == 0 && ndev.dhcp_stage == 1 && d->op == 2 && !ip_is_zero(d->yiaddr))
    mtype = 2;

  if(mtype == 2 && ndev.dhcp_stage == 1){
    memmove(ndev.dhcp_offer_ip, d->yiaddr, 4);
    if(ip_is_zero(server))
      memmove(server, d->siaddr, 4);
    memmove(ndev.dhcp_server_ip, server, 4);
    ndev.dhcp_offer_seen++;
    ndev.stats.dhcp_offer++;
    ndev.dhcp_stage = 2;
    if(!ip_is_zero(ndev.dhcp_offer_ip) && !ip_is_zero(ndev.dhcp_server_ip))
      net_dhcp_send(3, ndev.dhcp_offer_ip, ndev.dhcp_server_ip);
    return;
  }

  if(mtype == 5 && ndev.dhcp_stage >= 2){
    memmove(ndev.ip, d->yiaddr, 4);
    memmove(ndev.mask, mask, 4);
    memmove(ndev.gw, gw, 4);
    memmove(ndev.dns, dns, 4);
    ndev.lease_sec = lease;
    ndev.dhcp_ok = 1;
    ndev.dhcp_stage = 3;
    ndev.dhcp_ack_seen++;
    ndev.dhcp_last_err = 0;
    ndev.stats.dhcp_ack++;
  }
}

static void
net_handle_udp(uchar *frame, int len, struct ip4_hdr *ip, int ihl)
{
  struct udp_hdr *udp;
  int iptot;
  int udplen;
  int paylen;
  int maxpay;
  uchar *payload;

  if(len < (int)(sizeof(struct eth_hdr) + ihl + sizeof(struct udp_hdr)))
    return;
  iptot = ntohs(ip->tot_len);
  // 与 Linux skb 一致：网际头声明长度不得超过本帧内实际可用（避免真机/NIC 截断或声明过大导致越界读）。
  if((int)sizeof(struct eth_hdr) + iptot > len)
    iptot = len - (int)sizeof(struct eth_hdr);
  if(iptot < ihl + (int)sizeof(struct udp_hdr))
    return;
  udp = (struct udp_hdr*)((uchar*)ip + ihl);
  udplen = ntohs(udp->len);
  if(udplen < (int)sizeof(struct udp_hdr) || udplen > iptot - ihl)
    return;
  payload = (uchar*)udp + sizeof(struct udp_hdr);
  paylen = udplen - sizeof(struct udp_hdr);
  maxpay = len - (int)(sizeof(struct eth_hdr) + ihl + sizeof(struct udp_hdr));
  if(maxpay < 0)
    return;
  if(paylen > maxpay)
    paylen = maxpay;

  if(net_dhcp_dbg_enabled()){
    cprintf("net: udp %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d pay=%d\n",
            ip->src[0], ip->src[1], ip->src[2], ip->src[3], ntohs(udp->sport),
            ip->dst[0], ip->dst[1], ip->dst[2], ip->dst[3], ntohs(udp->dport), paylen);
    ndev.dhcp_dbg_budget--;
  }
  if(ntohs(udp->sport) == DHCP_SERVER_PORT && ntohs(udp->dport) == DHCP_CLIENT_PORT){
    memmove(ndev.dhcp_last_src_ip, ip->src, 4);
    net_handle_dhcp((struct dhcp_pkt*)payload, paylen);
  }
}

static void
net_handle_icmp(uchar *frame, int len, struct ip4_hdr *ip, int ihl)
{
  struct eth_hdr *eh = (struct eth_hdr*)frame;
  struct icmp_echo *icmp;
  int iptot;
  int icmp_len;
  uchar *payload;
  int payload_len;

  if(len < (int)(sizeof(struct eth_hdr) + ihl + sizeof(struct icmp_echo)))
    return;
  iptot = ntohs(ip->tot_len);
  if(iptot < ihl + (int)sizeof(struct icmp_echo))
    return;
  icmp = (struct icmp_echo*)((uchar*)ip + ihl);
  icmp_len = iptot - ihl;
  payload = (uchar*)icmp + sizeof(*icmp);
  payload_len = icmp_len - sizeof(*icmp);
  if(payload_len < 0)
    payload_len = 0;

  if(icmp->type == ICMP_ECHO_REQ){
    if(ip_eq(ip->dst, ndev.ip))
      net_send_icmp_pkt(eh->src, ndev.ip, ip->src, ICMP_ECHO_REPLY,
                        ntohs(icmp->ident), ntohs(icmp->seq),
                        payload, payload_len);
    return;
  }
}

static void
net_handle_arp(uchar *frame, int len)
{
  struct arp_pkt *ap;
  struct eth_hdr *eh;
  if(len < (int)(sizeof(struct eth_hdr) + sizeof(struct arp_pkt)))
    return;
  eh = (struct eth_hdr*)frame;
  ap = (struct arp_pkt*)(frame + sizeof(*eh));
  if(ntohs(ap->htype) != ARP_HTYPE_ETH || ntohs(ap->ptype) != ARP_PTYPE_IPV4)
    return;
  ndev.stats.rx_arp++;
  memmove(ndev.arp_peer_mac, ap->sha, 6);
  memmove(ndev.arp_peer_ip, ap->spa, 4);
  ndev.arp_valid = 1;
  if(ntohs(ap->op) == ARP_OP_REQ && ip_eq(ap->tpa, ndev.ip))
    net_send_arp_reply(ap->sha, ap->spa);
}

static void
net_handle_ipv4(uchar *frame, int len)
{
  struct eth_hdr *eh = (struct eth_hdr*)frame;
  struct ip4_hdr *ip;
  int ihl;
  int dhcp_to_client;
  if(len < (int)(sizeof(struct eth_hdr) + sizeof(struct ip4_hdr)))
    return;
  ip = (struct ip4_hdr*)(frame + sizeof(*eh));
  ihl = (ip->vhl & 0x0F) * 4;
  if(ihl < (int)sizeof(struct ip4_hdr))
    return;
  dhcp_to_client = udp_is_dhcp_to_client(ip, len, ihl);
  // 在目的地址过滤之前统计：凡能解析出合法 IPv4+UDP 头的均计入（含 SSDP 多播等），
  // 避免真机已收包但 rx_udp 仍为 0 的误判；与「本机需处理」的报文区分开。
  if(ip->proto == IP_PROTO_UDP &&
     len >= (int)(sizeof(struct eth_hdr) + ihl + sizeof(struct udp_hdr))){
    int iptot = ntohs(ip->tot_len);
    if(iptot >= ihl + (int)sizeof(struct udp_hdr))
      ndev.stats.rx_udp++;
  }
  if(!ip_eq(ip->dst, ndev.ip) &&
     !(ip->proto == IP_PROTO_UDP && ip_is_broadcast(ip->dst)) &&
     !dhcp_to_client &&
     !ip_eq(ip->dst, ip_loopback)){
    if(net_dhcp_dbg_enabled()){
      cprintf("net: drop ipv4 proto=%d src=%d.%d.%d.%d dst=%d.%d.%d.%d (not-for-us)\n",
              ip->proto,
              ip->src[0], ip->src[1], ip->src[2], ip->src[3],
              ip->dst[0], ip->dst[1], ip->dst[2], ip->dst[3]);
      ndev.dhcp_dbg_budget--;
    }
    return;
  }
  ndev.stats.rx_ipv4++;
  memmove(ndev.arp_peer_ip, ip->src, 4);
  memmove(ndev.arp_peer_mac, eh->src, 6);
  ndev.arp_valid = 1;
  if(ip->proto == IP_PROTO_UDP)
    net_handle_udp(frame, len, ip, ihl);
  else if(ip->proto == IP_PROTO_ICMP)
    net_handle_icmp(frame, len, ip, ihl);
}

static void
net_process_frame(uchar *frame, int len)
{
  struct eth_hdr *eh;
  ushort etype;
  ushort inner;
  ushort etype2;
  int vlan_depth;
  int i;
  struct ip4_hdr *ip;
  int ihl;
  struct udp_hdr *udp;
  if(len < (int)sizeof(struct eth_hdr))
    return;
  eh = (struct eth_hdr*)frame;
  etype = ntohs(eh->etype);
  // X220/82579LM can occasionally present L2 data with a 2-byte prefix.
  // If current ethertype is unknown but offset+2 matches known types, realign.
  if(!net_known_etype(etype) && len >= 16){
    etype2 = ((ushort)frame[14] << 8) | frame[15];
    if(net_known_etype(etype2)){
      if(net_dhcp_dbg_enabled()){
        cprintf("net: rx l2fix+2 etype0=%x etype2=%x len=%d\n", etype, etype2, len);
        ndev.dhcp_dbg_budget--;
      }
      memmove(frame, frame + 2, len - 2);
      len -= 2;
      eh = (struct eth_hdr*)frame;
      etype = ntohs(eh->etype);
    }
  }
  net_l2_try_8023_snap_ipv4(frame, &len);
  net_l2_align_ipv4(frame, &len);
  eh = (struct eth_hdr*)frame;
  etype = ntohs(eh->etype);
  vlan_depth = 0;
  while((etype == ETH_TYPE_VLAN || etype == ETH_TYPE_VLAN_QINQ) && len >= 18 && vlan_depth < 4){
    inner = ((ushort)frame[16] << 8) | frame[17];
    memmove(frame + 14, frame + 18, len - 18);
    len -= 4;
    eh->etype = htons(inner);
    etype = inner;
    vlan_depth++;
  }
  if(etype == ETH_TYPE_LLDP || etype == ETH_TYPE_IPV6)
    return;
  if(etype == ETH_TYPE_L2_VENDOR_LOOP)
    return;
  if(net_dhcp_dbg_enabled()){
    if(etype == ETH_TYPE_IPV4 && len >= (int)(sizeof(struct eth_hdr) + sizeof(struct ip4_hdr))){
      ip = (struct ip4_hdr*)(frame + sizeof(struct eth_hdr));
      ihl = (ip->vhl & 0x0F) * 4;
      if(ip->proto == IP_PROTO_UDP && len >= (int)(sizeof(struct eth_hdr) + ihl + sizeof(struct udp_hdr))){
        udp = (struct udp_hdr*)((uchar*)ip + ihl);
        cprintf("net: rx ipv4 udp %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d len=%d\n",
                ip->src[0], ip->src[1], ip->src[2], ip->src[3], ntohs(udp->sport),
                ip->dst[0], ip->dst[1], ip->dst[2], ip->dst[3], ntohs(udp->dport), len);
      } else {
        cprintf("net: rx ipv4 proto=%d len=%d\n", ip->proto, len);
      }
    } else if(etype == ETH_TYPE_ARP){
      cprintf("net: rx arp len=%d\n", len);
    } else {
      cprintf("net: rx etype=%x len=%d dmac=%x:%x:%x:%x:%x:%x smac=%x:%x:%x:%x:%x:%x\n",
              etype, len,
              frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
              frame[6], frame[7], frame[8], frame[9], frame[10], frame[11]);
      cprintf("net: rx head");
      for(i = 0; i < 24 && i < len; i++)
        cprintf(" %x", frame[i]);
      cprintf("\n");
      net_l2_debug_hint_ipv4_offset(frame, len);
    }
    ndev.dhcp_dbg_budget--;
  }
  // Learn potential gateway hint from SSDP only（与 Linux 用户态发现路由器方式一致，避免误学随机广播）。
  if(etype == ETH_TYPE_IPV4 && len >= (int)(sizeof(struct eth_hdr) + sizeof(struct ip4_hdr))){
    ip = (struct ip4_hdr*)(frame + sizeof(struct eth_hdr));
    ihl = (ip->vhl & 0x0F) * 4;
    if(ihl >= (int)sizeof(struct ip4_hdr) &&
       (ip_is_multicast(ip->dst) || ip_is_broadcast(ip->dst)) &&
       !ip_is_zero(ip->src) &&
       udp_is_ssdp(ip, len, ihl) &&
       !udp_is_dhcp_to_server(ip, len, ihl) &&
       !udp_is_dhcp_to_client(ip, len, ihl)){
      memmove(ndev.dhcp_hint_ip, ip->src, 4);
      memmove(ndev.dhcp_hint_mac, frame + 6, 6);
      if(net_dhcp_dbg_enabled()){
        cprintf("dhcp: hint %d.%d.%d.%d mac=%x:%x:%x:%x:%x:%x\n",
                ndev.dhcp_hint_ip[0], ndev.dhcp_hint_ip[1], ndev.dhcp_hint_ip[2], ndev.dhcp_hint_ip[3],
                ndev.dhcp_hint_mac[0], ndev.dhcp_hint_mac[1], ndev.dhcp_hint_mac[2],
                ndev.dhcp_hint_mac[3], ndev.dhcp_hint_mac[4], ndev.dhcp_hint_mac[5]);
        ndev.dhcp_dbg_budget--;
      }
    }
  }
  if(etype == ETH_TYPE_ARP)
    net_handle_arp(frame, len);
  else if(etype == ETH_TYPE_IPV4)
    net_handle_ipv4(frame, len);
  else if(net_dhcp_dbg_enabled()){
    cprintf("net: drop l2 etype=%x len=%d (unhandled)\n", etype, len);
    ndev.dhcp_dbg_budget--;
  }
}

static void
netif_process_rxq(void)
{
  uchar frame[NET_PKT_BUF];
  int len;

  for(;;){
    acquire(&ndev.lock);
    if(ndev.rxq_cnt == 0){
      release(&ndev.lock);
      break;
    }
    len = ndev.rxq_len[ndev.rxq_head];
    if(len > NET_PKT_BUF)
      len = NET_PKT_BUF;
    memmove(frame, ndev.rxq[ndev.rxq_head], len);
    ndev.rxq_head = (ndev.rxq_head + 1) % NETIF_RXQ;
    ndev.rxq_cnt--;
    release(&ndev.lock);

    if(ndev.use_microps)
      sirpair_microps_deliver_ether(frame, len);
    else
      net_process_frame(frame, len);
  }
}

static void
net_selftest(void)
{
  uchar frame[128];
  struct eth_hdr *eh = (struct eth_hdr*)frame;
  struct arp_pkt *ap = (struct arp_pkt*)(frame + sizeof(*eh));

  // ARP request: who-has local ip?
  memset(frame, 0, sizeof(frame));
  memset(eh->dst, 0xFF, 6);
  eh->src[0] = 0x52; eh->src[1] = 0x54; eh->src[2] = 0x00;
  eh->src[3] = 0x12; eh->src[4] = 0x34; eh->src[5] = 0x99;
  eh->etype = htons(ETH_TYPE_ARP);
  ap->htype = htons(ARP_HTYPE_ETH);
  ap->ptype = htons(ARP_PTYPE_IPV4);
  ap->hlen = 6;
  ap->plen = 4;
  ap->op = htons(ARP_OP_REQ);
  memmove(ap->sha, eh->src, 6);
  ap->spa[0] = 10; ap->spa[1] = 0; ap->spa[2] = 2; ap->spa[3] = 2;
  memmove(ap->tpa, ndev.ip, 4);
  net_process_frame(frame, sizeof(struct eth_hdr) + sizeof(struct arp_pkt));

}

void
net_poll(void)
{
  uint idx;
  struct rxdesc *d;
  int len;

  if(!ndev.initialized)
    return;

  acquire(&ndev.lock);
  net_tx_clean();
  // Linux e1000e 使用 next_to_clean 顺序取描述符；用 (RDT+1) 会在 RDH/RDT 与真机不同步时跳过
  // 尚未处理的描述符（例如 RDH=8、RDT=7 时仍应从 rx_clean 起顺序检查 DD）。
  idx = ndev.rx_clean;
  for(;;){
    uint staterr;
    d = &net_rx_ring[idx];
    clflush_range(d, sizeof(*d));
    mfence();
    if(ndev.rx_wb_ext){
      staterr = d->u.ext.status_error;
      if((staterr & E1000_RXD_STAT_DD) == 0)
        break;
    } else {
      if((d->u.leg.status & E1000_RXD_STAT_DD) == 0)
        break;
      staterr = (uint)d->u.leg.status;
    }
    if(net_dhcp_dbg_enabled()){
      if(ndev.rx_wb_ext)
        len = (int)(d->u.ext.length & 0x3FFFU);
      else
        len = (int)(d->u.leg.len & 0x3FFFU);
      cprintf("net: rx desc idx=%d ext=%d staterr=%x len=%d rdh=%x rdt=%x\n",
              idx, ndev.rx_wb_ext, staterr, len, nr(E1000_RDH), nr(E1000_RDT));
      ndev.dhcp_dbg_budget--;
    }
    if(ndev.rx_wb_ext){
      if(((staterr >> 8) & 0xFF) && net_dhcp_dbg_enabled()){
        cprintf("net: rx desc err idx=%d staterr=%x len=%d vlan=%x\n",
                idx, staterr, (int)(d->u.ext.length & 0x3FFFU), d->u.ext.vlan);
        ndev.dhcp_dbg_budget--;
      }
    } else if(d->u.leg.errors && net_dhcp_dbg_enabled()){
      cprintf("net: rx desc err idx=%d status=%x err=%x len=%d special=%x\n",
              idx, d->u.leg.status, d->u.leg.errors,
              (int)(d->u.leg.len & 0x3FFFU), d->u.leg.special);
      ndev.dhcp_dbg_budget--;
    }
    if(ndev.rx_wb_ext){
      if(!(staterr & E1000_RXD_STAT_EOP))
        ndev.stats.rx_drop++;
      else {
        len = (int)(d->u.ext.length & 0x3FFFU);
        if(len > NET_PKT_BUF){
          ndev.stats.rx_drop++;
        } else if(ndev.rxq_cnt >= NETIF_RXQ){
          ndev.stats.rx_drop++;
        } else {
          clflush_range(ndev.rxbuf[idx], NET_PKT_BUF);
          mfence();
          memmove(ndev.rxq[ndev.rxq_tail], ndev.rxbuf[idx], len);
          ndev.rxq_len[ndev.rxq_tail] = len;
          ndev.rxq_tail = (ndev.rxq_tail + 1) % NETIF_RXQ;
          ndev.rxq_cnt++;
          ndev.stats.rx_pkts++;
        }
      }
    } else {
      if(!(d->u.leg.status & E1000_RXD_STAT_EOP)){
        ndev.stats.rx_drop++;
      } else {
        len = (int)((ushort)d->u.leg.len & 0x3FFFU);
        if(len > NET_PKT_BUF){
          ndev.stats.rx_drop++;
        } else if(ndev.rxq_cnt >= NETIF_RXQ){
          ndev.stats.rx_drop++;
        } else {
          clflush_range(ndev.rxbuf[idx], (uint)len);
          mfence();
          memmove(ndev.rxq[ndev.rxq_tail], ndev.rxbuf[idx], len);
          ndev.rxq_len[ndev.rxq_tail] = len;
          ndev.rxq_tail = (ndev.rxq_tail + 1) % NETIF_RXQ;
          ndev.rxq_cnt++;
          ndev.stats.rx_pkts++;
        }
      }
    }
    memset(&d->u, 0, sizeof(d->u));
    net_write_rdt(idx);
    idx = (idx + 1) % NET_RX_RING;
  }
  ndev.rx_clean = idx;
  release(&ndev.lock);

  netif_process_rxq();
}

void
net_intr(void)
{
  if(!ndev.initialized)
    return;
  nr(E1000_ICR);
  net_poll();
}

void
netinit(void)
{
  initlock(&ndev.lock, "net");
  ndev.initialized = 0;
  ndev.irq = -1;
  ndev.mmio = 0;
  ndev.arp_valid = 0;
  ndev.tcp_isn = 0x12345000;
  ndev.dhcp_xid = 0;
  ndev.dhcp_stage = 0;
  ndev.dhcp_ok = 0;
  ndev.dhcp_attempts = 0;
  ndev.dhcp_offer_seen = 0;
  ndev.dhcp_ack_seen = 0;
  ndev.dhcp_last_err = 0;
  ndev.dhcp_discover_tx = 0;
  ndev.dhcp_request_tx = 0;
  ndev.dhcp_dbg_until = 0;
  ndev.dhcp_dbg_budget = 0;
  ndev.io_trace_budget = 0;
  ndev.use_microps = 0;
  memset(ndev.dhcp_offer_ip, 0, sizeof(ndev.dhcp_offer_ip));
  memset(ndev.dhcp_server_ip, 0, sizeof(ndev.dhcp_server_ip));
  memset(ndev.dhcp_last_src_ip, 0, sizeof(ndev.dhcp_last_src_ip));
  ndev.lease_sec = 0;
  memset(&ndev.stats, 0, sizeof(ndev.stats));
  ndev.rx_wb_ext = 0;
  memset(ndev.ip, 0, sizeof(ndev.ip));
  memset(ndev.mask, 0, sizeof(ndev.mask));
  memset(ndev.gw, 0, sizeof(ndev.gw));
  memset(ndev.dns, 0, sizeof(ndev.dns));

  if(net_probe_pci() < 0){
    cprintf("net: no supported wired nic found");
    boot_fail();
    return;
  }
  pci_setup_device(ndev.bus, ndev.dev, ndev.func);
  net_hw_reset();
  net_read_mac();
  net_init_tx();
  net_init_rx();

  // Polling-first strategy for stability on QEMU and X220.
  nw(E1000_IMC, 0xFFFFFFFF);
  if(ismp && ndev.irq > 0)
    ioapicenable_level(ndev.irq, mpbcpu());

  ndev.initialized = 1;
  ndev.io_trace_budget = 0;
  net_selftest();
  cprintf("net: nic %x:%x at %d:%d.%d irq %d mac %x:%x:%x:%x:%x:%x ip %d.%d.%d.%d manc=%x",
          ndev.vendor, ndev.device, ndev.bus, ndev.dev, ndev.func, ndev.irq,
          ndev.mac[0], ndev.mac[1], ndev.mac[2], ndev.mac[3], ndev.mac[4], ndev.mac[5],
          ndev.ip[0], ndev.ip[1], ndev.ip[2], ndev.ip[3], nr(E1000_MANC));
  boot_ok();
  cprintf("net: regs ctrl=%x ctrl_ext=%x status=%x rctl=%x tctl=%x rdh=%x rdt=%x tdh=%x tdt=%x",
          nr(E1000_CTRL), nr(E1000_CTRL_EXT), nr(E1000_STATUS),
          nr(E1000_RCTL), nr(E1000_TCTL), nr(E1000_RDH), nr(E1000_RDT), nr(E1000_TDH), nr(E1000_TDT));
  boot_ok();
  cprintf("net: qregs rxdctl=%x txdctl=%x", nr(E1000_RXDCTL), nr(E1000_TXDCTL));
  boot_ok();
  cprintf("net: pci cmd=%x status=%x",
          pci_read16(ndev.bus, ndev.dev, ndev.func, PCI_COMMAND),
          pci_read16(ndev.bus, ndev.dev, ndev.func, PCI_STATUS));
  boot_ok();
  cprintf("net: dma rings tdbal=%x rdbal=%x align128=%d %d",
          V2P(net_tx_ring), V2P(net_rx_ring),
          V2P(net_tx_ring) & 127, V2P(net_rx_ring) & 127);
  boot_ok();
  if(ndev.device == 0x1502 || ndev.device == 0x1503){
    cprintf("net: pch fwsm=%x pch_me_tail_arb=%d",
            nr(E1000_FWSM), net_pch_pcim2pci_wa_needed());
    boot_ok();
  }
}

int
net_is_available(void)
{
  return ndev.initialized;
}

int
net_get_irq(void)
{
  return ndev.initialized ? ndev.irq : -1;
}

void
net_get_mac(uchar mac[6])
{
  int i;
  for(i = 0; i < 6; i++)
    mac[i] = ndev.mac[i];
}

void
net_get_ipv4(uchar ip[4])
{
  int i;
  for(i = 0; i < 4; i++)
    ip[i] = ndev.ip[i];
}

void
net_get_dhcp_cfg(uchar gw[4], uchar mask[4], uchar dns[4], uint *lease_sec, int *ok)
{
  int i;
  if(gw) for(i = 0; i < 4; i++) gw[i] = ndev.gw[i];
  if(mask) for(i = 0; i < 4; i++) mask[i] = ndev.mask[i];
  if(dns) for(i = 0; i < 4; i++) dns[i] = ndev.dns[i];
  if(lease_sec) *lease_sec = ndev.lease_sec;
  if(ok) *ok = ndev.dhcp_ok;
}

int
net_dhcp_acquire(void)
{
  uint start;
  uint now;
  uint last_tx;
  int sent_req_once;
  int saw_link;
  int grace;
  int prev_mp;

  if(!ndev.initialized)
    return -1;

  /*
   * 引导成功后 net_set_microps_l3(1) 时，收包只进 microps；而 DHCP 状态机在 net_handle_dhcp
   *（legacy net_process_frame 路径）。用户态再次 dhcp() 时必须暂时关闭 microps 交付，
   * 否则 Discover/Request 发出后 Offer/Ack 无法交给内核处理。
   */
  prev_mp = ndev.use_microps;
  net_set_microps_l3(0);

  net_tx_soft_reset();

  ndev.dhcp_attempts++;
  ndev.dhcp_xid = DHCP_XID_BASE ^ ticks ^ (ndev.mac[3] << 8) ^ ndev.mac[5];
  ndev.dhcp_stage = 1;
  ndev.dhcp_ok = 0;
  ndev.dhcp_last_err = -2; // waiting
  ndev.dhcp_dbg_until = ticks + 1000; // debug window around one acquisition
  ndev.dhcp_dbg_budget = 48;
  ndev.io_trace_budget = 32; // 逐包 net: io tx/rx 上限，仅与上面调试窗口同时生效
  memset(ndev.dhcp_offer_ip, 0, sizeof(ndev.dhcp_offer_ip));
  memset(ndev.dhcp_server_ip, 0, sizeof(ndev.dhcp_server_ip));
  net_dhcp_send(1, 0, 0); // discover
  sent_req_once = 0;
  saw_link = net_link_up();
  cprintf("dhcp: begin xid=%x link=%d status=%x mac=%x:%x:%x:%x:%x:%x\n",
          ndev.dhcp_xid, saw_link, nr(E1000_STATUS),
          ndev.mac[0], ndev.mac[1], ndev.mac[2], ndev.mac[3], ndev.mac[4], ndev.mac[5]);
  cprintf("dhcp: regs rctl=%x tctl=%x rdh=%x rdt=%x tdh=%x tdt=%x ctrl_ext=%x manc=%x\n",
          nr(E1000_RCTL), nr(E1000_TCTL), nr(E1000_RDH), nr(E1000_RDT),
          nr(E1000_TDH), nr(E1000_TDT), nr(E1000_CTRL_EXT), nr(E1000_MANC));
  cprintf("dhcp: qregs rxdctl=%x txdctl=%x pcicmd=%x pcists=%x\n",
          nr(E1000_RXDCTL), nr(E1000_TXDCTL),
          pci_read16(ndev.bus, ndev.dev, ndev.func, PCI_COMMAND),
          pci_read16(ndev.bus, ndev.dev, ndev.func, PCI_STATUS));

  acquire(&tickslock);
  start = ticks;
  last_tx = start;
  release(&tickslock);
  for(;;){
    net_poll();
    acquire(&tickslock);
    now = ticks;
    release(&tickslock);
    if(net_link_up())
      saw_link = 1;

    // 约每秒重试：阶段 1 发发现，阶段 2 发请求。
    if(now - last_tx > 100){
      if(ndev.dhcp_stage == 1){
        uchar bcast_ip[4];
        bcast_ip[0] = bcast_ip[1] = bcast_ip[2] = bcast_ip[3] = 255;
        net_dhcp_send(1, 0, 0);
        // 广播发现之后仍无应答时，部分家用路由仅对「二层目的地址为自身 MAC」的发现帧做应答；
        // 三层仍为广播、端口仍为 68->67，与常见固件行为一致（在已从 SSDP 学到路由器时启用）。
        if(ndev.dhcp_discover_tx >= 5 &&
           !mac_is_zero(ndev.dhcp_hint_mac) &&
           !ip_is_zero(ndev.dhcp_hint_ip)){
          net_dhcp_send_to(ndev.dhcp_hint_mac, bcast_ip, 1, 0, 0);
          if(net_dhcp_dbg_enabled()){
            cprintf("dhcp: extra L2-unicast discover -> hint %d.%d.%d.%d mac=%x:%x:%x:%x:%x:%x\n",
                    ndev.dhcp_hint_ip[0], ndev.dhcp_hint_ip[1],
                    ndev.dhcp_hint_ip[2], ndev.dhcp_hint_ip[3],
                    ndev.dhcp_hint_mac[0], ndev.dhcp_hint_mac[1], ndev.dhcp_hint_mac[2],
                    ndev.dhcp_hint_mac[3], ndev.dhcp_hint_mac[4], ndev.dhcp_hint_mac[5]);
            ndev.dhcp_dbg_budget--;
          }
        }
      } else if(ndev.dhcp_stage == 2 &&
              !ip_is_zero(ndev.dhcp_offer_ip) &&
              !ip_is_zero(ndev.dhcp_server_ip)){
        net_dhcp_send(3, ndev.dhcp_offer_ip, ndev.dhcp_server_ip);
        sent_req_once = 1;
      }
      last_tx = now;
    }
    if(ndev.dhcp_stage == 3)
      break;
    if(now - start > 4000) // 约 40s @100Hz，真机/家用路由应答可能较慢
      break;
  }

  // 主循环因超时退出后，再轮询一段时间，避免 Offer 稍晚到达却被判失败。
  if(!(ndev.dhcp_stage == 3 && ndev.dhcp_ok)){
    for(grace = 0; grace < 400; grace++){
      net_poll();
      if(ndev.dhcp_stage == 3 && ndev.dhcp_ok)
        break;
    }
  }

  if(ndev.dhcp_stage == 3 && ndev.dhcp_ok)
  {
    cprintf("dhcp: ok xid=%x ip=%d.%d.%d.%d gw=%d.%d.%d.%d offer=%d ack=%d\n",
            ndev.dhcp_xid,
            ndev.ip[0], ndev.ip[1], ndev.ip[2], ndev.ip[3],
            ndev.gw[0], ndev.gw[1], ndev.gw[2], ndev.gw[3],
            ndev.dhcp_offer_seen, ndev.dhcp_ack_seen);
    ndev.dhcp_dbg_until = 0;
    ndev.io_trace_budget = 0;
    if(sirpair_e1000_sync_ndev_cfg() < 0){
      cprintf("dhcp: microps wired sync failed (microps stack may be stale)\n");
      net_set_microps_l3(prev_mp);
    } else
      net_set_microps_l3(1);
    return 0;
  }

  // Do not fabricate a deterministic gateway/IP on timeout.
  // User-space should only see DHCP success when we really got Offer/Ack.
  if(ndev.dhcp_stage == 1)
    ndev.dhcp_last_err = -3; // no offer
  else if(ndev.dhcp_stage == 2)
    ndev.dhcp_last_err = sent_req_once ? -4 : -5; // no ack / bad offer
  else
    ndev.dhcp_last_err = -6; // unknown
  if(!saw_link)
    ndev.dhcp_last_err = -7; // link never up
  cprintf("dhcp: fail xid=%x stage=%d err=%d tx=%d rx=%d udp=%d discover=%d request=%d status=%x\n",
          ndev.dhcp_xid, ndev.dhcp_stage, ndev.dhcp_last_err,
          ndev.stats.tx_pkts, ndev.stats.rx_pkts, ndev.stats.rx_udp,
          ndev.dhcp_discover_tx, ndev.dhcp_request_tx, nr(E1000_STATUS));
  cprintf("dhcp: failregs rctl=%x tctl=%x rdh=%x rdt=%x tdh=%x tdt=%x ctrl_ext=%x manc=%x\n",
          nr(E1000_RCTL), nr(E1000_TCTL), nr(E1000_RDH), nr(E1000_RDT),
          nr(E1000_TDH), nr(E1000_TDT), nr(E1000_CTRL_EXT), nr(E1000_MANC));
  cprintf("dhcp: failqregs rxdctl=%x txdctl=%x pcicmd=%x pcists=%x\n",
          nr(E1000_RXDCTL), nr(E1000_TXDCTL),
          pci_read16(ndev.bus, ndev.dev, ndev.func, PCI_COMMAND),
          pci_read16(ndev.bus, ndev.dev, ndev.func, PCI_STATUS));
  net_tx_soft_reset();
  ndev.dhcp_ok = 0;
  ndev.dhcp_dbg_until = 0;
  ndev.io_trace_budget = 0;
  net_set_microps_l3(prev_mp);
  return -1;
}

void
net_set_microps_l3(int enable)
{
  ndev.use_microps = enable ? 1 : 0;
}

int
net_get_microps_l3(void)
{
  return ndev.use_microps;
}

int
net_ping_interrupted(void)
{
  return proc != 0 && proc->killed;
}

int
net_ping(uint dst_ip_u32, int count)
{
  if(!ndev.initialized)
    return -1;
  return sirpair_microps_icmp_ping(dst_ip_u32, count);
}


void
net_get_cfg(struct net_cfg *cfg)
{
  int i;

  if(cfg == 0)
    return;
  memset(cfg, 0, sizeof(*cfg));
  for(i = 0; i < 4; i++){
    cfg->ip[i] = ndev.ip[i];
    cfg->gw[i] = ndev.gw[i];
    cfg->mask[i] = ndev.mask[i];
    cfg->dns[i] = ndev.dns[i];
    cfg->dhcp_offer_ip[i] = ndev.dhcp_offer_ip[i];
    cfg->dhcp_server_ip[i] = ndev.dhcp_server_ip[i];
    cfg->dhcp_last_src_ip[i] = ndev.dhcp_last_src_ip[i];
  }
  cfg->lease_sec = ndev.lease_sec;
  cfg->dhcp_ok = ndev.dhcp_ok;
  cfg->dhcp_stage = ndev.dhcp_stage;
  cfg->dhcp_last_err = ndev.dhcp_last_err;
  cfg->dhcp_xid = ndev.dhcp_xid;
  cfg->dhcp_attempts = ndev.dhcp_attempts;
  cfg->dhcp_offer_seen = ndev.dhcp_offer_seen;
  cfg->dhcp_ack_seen = ndev.dhcp_ack_seen;
  cfg->dhcp_discover_tx = ndev.dhcp_discover_tx;
  cfg->dhcp_request_tx = ndev.dhcp_request_tx;
  cfg->link_status = ndev.mmio ? nr(E1000_STATUS) : 0;
  cfg->link_up = (cfg->link_status & E1000_STATUS_LU) != 0;
  cfg->tx_pkts = ndev.stats.tx_pkts;
  cfg->rx_pkts = ndev.stats.rx_pkts;
  cfg->rx_udp = ndev.stats.rx_udp;
  memmove(cfg->mac, ndev.mac, sizeof(cfg->mac));
  sirpair_net_cfg_loopback(cfg);
}

void
net_get_stats(struct net_stats *st)
{
  if(st == 0)
    return;
  *st = ndev.stats;
}

