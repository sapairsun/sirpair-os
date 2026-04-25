#include "types.h"
#include "defs.h"
#include "x86.h"
#include "param.h"
#include "mmu.h"
#include "memlayout.h"
#include "proc.h"
#include "microps_kernel.h"
#include "util.h"
#include "microps_net.h"
#include "ip.h"
#include "icmp.h"
#include "driver/null.h"
#include "driver/loopback.h"
#include "net.h"
#include "spinlock.h"

extern int sirpair_e1000_register_iface(const uchar *ip, const uchar *mask, const uchar *gw);

static int microps_core_inited;
static int microps_net_running;
static volatile int mp_ping_wait;
static volatile int mp_ping_got;
static ushort mp_ping_id;
static ushort mp_ping_seq;

void
microps_raise_softirq(void)
{
  net_protocol_handler();
}

void
sirpair_on_icmp_echoreply(uint16_t ident, uint16_t seq)
{
  if(!mp_ping_wait)
    return;
  if(ident == mp_ping_id && seq == mp_ping_seq){
    mp_ping_got = 1;
    return;
  }
  {
    static int echo_mismatch_dbg;
    if(echo_mismatch_dbg < 8){
      cprintf("ping debug: reply/request mismatch got id=0x%x seq=%u expect id=0x%x seq=%u\n",
              ident, (uint)seq, (uint)mp_ping_id, (uint)mp_ping_seq);
      echo_mismatch_dbg++;
    }
  }
}

void
sirpair_microps_poll_timers(void)
{
  if(!microps_net_running)
    return;
  net_timer_handler();
}

static struct net_device *sirpair_loop_dev;

static int
sirpair_microps_core_init(void)
{
  struct ip_iface *iface;

  if(microps_core_inited)
    return 0;
  kmalloc_init();
  if(net_init() != 0){
    cprintf("microps: net_init failed\n");
    return -1;
  }
  if(!null_init()){
    cprintf("microps: null_init failed\n");
    return -1;
  }
  sirpair_loop_dev = loopback_init();
  if(!sirpair_loop_dev){
    cprintf("microps: loopback_init failed\n");
    return -1;
  }
  iface = ip_iface_alloc("127.0.0.1", "255.0.0.0");
  if(!iface){
    cprintf("microps: ip_iface_alloc (loopback) failed\n");
    return -1;
  }
  if(ip_iface_register(sirpair_loop_dev, iface) != 0){
    cprintf("microps: ip_iface_register (loopback) failed\n");
    return -1;
  }
  microps_core_inited = 1;
  return 0;
}

void
sirpair_net_cfg_loopback(struct net_cfg *cfg)
{
  if(cfg == 0)
    return;
  cfg->lo_up = 0;
  if(!microps_core_inited)
    return;
  cfg->lo_up = 1;
  cfg->lo_ip[0] = 127;
  cfg->lo_ip[1] = 0;
  cfg->lo_ip[2] = 0;
  cfg->lo_ip[3] = 1;
  cfg->lo_mask[0] = 255;
  cfg->lo_mask[1] = 0;
  cfg->lo_mask[2] = 0;
  cfg->lo_mask[3] = 0;
}

void
sirpair_microps_boot_phase(int dhcp_ok)
{
  uchar ip[4];
  uchar gw[4];
  uchar mask[4];
  uint lease;
  int ok;

  if(sirpair_microps_core_init() < 0)
    return;
  if(dhcp_ok){
    net_get_ipv4(ip);
    net_get_dhcp_cfg(gw, mask, 0, &lease, &ok);
    (void)lease;
    (void)ok;
    if(sirpair_e1000_register_iface(ip, mask, gw) != 0){
      cprintf("microps: wired interface registration failed\n");
    } else {
      net_set_microps_l3(1);
      cprintf("microps: e1000e attached, using microps stack\n");
    }
  } else
    cprintf("microps: DHCP failed, loopback stack only\n");

  if(!microps_net_running){
    if(net_run() != 0){
      cprintf("microps: net_run failed\n");
      return;
    }
    microps_net_running = 1;
  }
}

int
sirpair_microps_icmp_ping(uint dst_ip_u32, int count)
{
  ip_addr_t src;
  ip_addr_t dst;
  uchar sbytes[4];
  uchar dbytes[4];
  static const uint8_t payload[] = "sirpair-microps";
  int i;
  int replies;
  uint start;
  uint now;

  if(!microps_net_running || !net_get_microps_l3())
    return -1;
  dbytes[0] = (uchar)((dst_ip_u32 >> 24) & 0xFFu);
  dbytes[1] = (uchar)((dst_ip_u32 >> 16) & 0xFFu);
  dbytes[2] = (uchar)((dst_ip_u32 >> 8) & 0xFFu);
  dbytes[3] = (uchar)(dst_ip_u32 & 0xFFu);
  memmove(&dst, dbytes, 4);
  net_get_ipv4(sbytes);
  memmove(&src, sbytes, 4);
  if(count <= 0)
    count = 4;
  if(count > 16)
    count = 16;
  replies = 0;
  for(i = 0; i < count; i++){
    if(net_ping_interrupted()){
      mp_ping_wait = 0;
      return -1;
    }
    /*
     * 禁止在此对「目的 IP」做 ARP：跨网段必须解析网关 MAC，由 icmp_output→ip_output
     * 根据路由表的 nexthop（默认网关）完成 arp_resolve。
     */
    mp_ping_id = 0x7876;
    mp_ping_seq = (ushort)(i + 1);
    mp_ping_got = 0;
    mp_ping_wait = 1;
    {
      int j;
      for(j = 0; j < 48; j++){
        if(icmp_output(ICMP_TYPE_ECHO, 0,
                       hton32(((uint32_t)mp_ping_id << 16) | (uint32_t)mp_ping_seq),
                       payload, sizeof(payload), src, dst) >= 0)
          break;
        net_poll();
        net_protocol_handler();
        net_timer_handler();
      }
      if(j >= 48){
        mp_ping_wait = 0;
        continue;
      }
    }
    acquire(&tickslock);
    start = ticks;
    release(&tickslock);
    {
      uint spin_iter;
      uint spin_dbg_last;
      spin_iter = 0;
      spin_dbg_last = 0;
      for(;;){
        if(net_ping_interrupted()){
          mp_ping_wait = 0;
          return -1;
        }
        net_poll();
        net_protocol_handler();
        net_timer_handler();
        if(mp_ping_got){
          replies++;
          break;
        }
        acquire(&tickslock);
        now = ticks;
        release(&tickslock);
        /*
         * 超时依赖全局 ticks（仅引导处理器上累加）。若本逻辑处理器长时间在
         * 自旋锁上忙等且无法处理定时器中断，ticks 可能几乎不前进，导致永远
         * 等不到基于 tick 的超时。此处增加轮询次数兜底并让出 CPU。
         */
        spin_iter++;
        if((spin_iter & 63u) == 0u)
          yield();
        if(spin_iter > 8000000u){
          cprintf("ping debug: single probe polling timeout (polls=%u), drop packet tick=%u start=%u\n",
                  spin_iter, ticks, start);
          break;
        }
        if(spin_iter - spin_dbg_last >= 200000u){
          cprintf("ping debug: waiting reply polls=%u tick=%u start=%u replied=%d cpu=%d\n",
                  spin_iter, ticks, start, mp_ping_got, cpu->id);
          spin_dbg_last = spin_iter;
        }
        if((int)(now - start) > HZ)
          break;
      }
    }
    mp_ping_wait = 0;
  }
  return replies;
}

void
sirpair_microps_selftest(void)
{
}
