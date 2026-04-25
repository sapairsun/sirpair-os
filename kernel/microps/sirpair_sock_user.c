/*
 * IPv4 用户态套接字：经 microps sock API，取代 net.c 内简易 TCP/UDP 状态机。
 */
#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "fs.h"
#include "file.h"
#include "proc.h"
#include "net.h"
#include "spinlock.h"
#include "sock.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "util.h"

#define DNS_SERVER_PORT 53

struct dns_hdr {
  ushort id;
  ushort flags;
  ushort qdcount;
  ushort ancount;
  ushort nscount;
  ushort arcount;
} __attribute__((packed));

static int
parse_ipv4_port_bind(const char *s, uchar ip[4], int *port_out)
{
  int i, v, ndig;
  const char *p = s;
  int port = 23;

  for(i = 0; i < 4; i++){
    v = 0;
    ndig = 0;
    while(*p >= '0' && *p <= '9'){
      v = v * 10 + (*p - '0');
      if(v > 255)
        return -1;
      p++;
      ndig++;
    }
    if(ndig == 0)
      return -1;
    ip[i] = (uchar)v;
    if(i < 3){
      if(*p != '.')
        return -1;
      p++;
    }
  }
  if(*p == ':'){
    p++;
    v = 0;
    ndig = 0;
    while(*p >= '0' && *p <= '9'){
      v = v * 10 + (*p - '0');
      if(v > 65535)
        return -1;
      p++;
      ndig++;
    }
    if(ndig == 0)
      return -1;
    port = v;
  } else if(*p != 0)
    return -1;
  *port_out = port;
  return 0;
}

static void
ip4_to_sockaddr(struct sockaddr_in *sa, uchar ip[4], int port_host)
{
  memset(sa, 0, sizeof(*sa));
  sa->sin_family = AF_INET;
  memmove(&sa->sin_addr, ip, 4);
  sa->sin_port = hton16((uint16_t)port_host);
}

static void
ip4_to_endpoint(struct ip_endpoint *ep, uchar ip[4], int port_host)
{
  memmove(&ep->addr, ip, 4);
  ep->port = hton16((uint16_t)port_host);
}

static int
ip_eq(uchar a[4], uchar b[4])
{
  return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}

static int
ip_is_zero(uchar a[4])
{
  return a[0]==0 && a[1]==0 && a[2]==0 && a[3]==0;
}

static int
ip_same_subnet(uchar a[4], uchar b[4], uchar m[4])
{
  int i;
  for(i = 0; i < 4; i++)
    if((a[i] & m[i]) != (b[i] & m[i]))
      return 0;
  return 1;
}

static int
dns_reply_src_ok(uchar src[4], uchar myip[4], uchar gw[4], uchar mask[4], uchar dns[4])
{
  if(ip_eq(src, dns) || ip_eq(src, gw))
    return 1;
  if(ip_same_subnet(myip, src, mask))
    return 1;
  return 0;
}

static int
dns_encode_name(const char *name, uchar *out, int maxlen)
{
  int i = 0;
  int lablen = 0;
  int labstart = -1;
  int n = strlen(name);
  int pos = 0;

  if(n <= 0 || n > 253)
    return -1;
  for(i = 0; i <= n; i++){
    char c = name[i];
    if(c == '.' || c == 0){
      if(lablen <= 0 || lablen > 63)
        return -1;
      if(pos + 1 + lablen >= maxlen)
        return -1;
      out[pos++] = (uchar)lablen;
      memmove(out + pos, name + labstart, lablen);
      pos += lablen;
      lablen = 0;
      labstart = -1;
      continue;
    }
    if(c == ' ' || c == '\t')
      return -1;
    if(lablen == 0)
      labstart = i;
    lablen++;
  }
  if(pos + 1 > maxlen)
    return -1;
  out[pos++] = 0;
  return pos;
}

static int
dns_skip_name(uchar *msg, int mlen, int off)
{
  int steps = 0;
  while(off < mlen){
    uchar c = msg[off];
    if(c == 0)
      return off + 1;
    if((c & 0xC0) == 0xC0){
      if(off + 1 >= mlen)
        return -1;
      return off + 2;
    }
    off += 1 + c;
    if(off > mlen)
      return -1;
    if(++steps > 64)
      return -1;
  }
  return -1;
}

static int
dns_parse_a_answer(uchar *payload, int paylen, ushort want_id, uchar *src_ip,
                   uchar myip[4], uchar gw[4], uchar mask[4], uchar dns[4], uint *out_ip)
{
  struct dns_hdr *h;
  int off;
  int i;
  ushort qd, an;

  if(paylen < (int)sizeof(struct dns_hdr))
    return -1;
  h = (struct dns_hdr *)payload;
  if(ntoh16(h->id) != want_id)
    return -1;
  if((ntoh16(h->flags) & 0x8000) == 0)
    return -1;
  if(!dns_reply_src_ok(src_ip, myip, gw, mask, dns))
    return -1;

  qd = ntoh16(h->qdcount);
  an = ntoh16(h->ancount);
  if(an == 0)
    return -1;

  off = sizeof(struct dns_hdr);
  for(i = 0; i < qd; i++){
    off = dns_skip_name(payload, paylen, off);
    if(off < 0 || off + 4 > paylen)
      return -1;
    off += 4;
  }

  for(i = 0; i < an; i++){
    ushort typ, cls, rdlen;
    off = dns_skip_name(payload, paylen, off);
    if(off < 0 || off + 10 > paylen)
      return -1;
    typ = ntoh16(*(ushort *)(payload + off)); off += 2;
    cls = ntoh16(*(ushort *)(payload + off)); off += 2;
    off += 4;
    rdlen = ntoh16(*(ushort *)(payload + off)); off += 2;
    if(off + rdlen > paylen)
      return -1;
    if(typ == 1 && rdlen == 4 && (cls == 1 || cls == 255)){
      *out_ip = ((uint)payload[off] << 24) | ((uint)payload[off+1] << 16) |
                ((uint)payload[off+2] << 8) | (uint)payload[off+3];
      return 0;
    }
    off += rdlen;
  }
  return -1;
}

int
net_tcp_user_alloc(void)
{
  if(!net_is_available() || !net_get_microps_l3())
    return -1;
  return tcp_open();
}

void
net_tcp_user_close(int slot)
{
  if(slot < 0)
    return;
  tcp_close(slot);
}

void
net_tcp_user_unref(int slot, int kind)
{
  (void)kind;
  net_tcp_user_close(slot);
}

int
net_tcp_user_bind(int slot, char *path)
{
  uchar ip[4];
  int port;
  struct ip_endpoint ep;

  if(slot < 0 || path == 0)
    return -1;
  if(parse_ipv4_port_bind(path, ip, &port) < 0)
    return -1;
  if(port <= 0 || port > 65535)
    return -1;
  ip4_to_endpoint(&ep, ip, port);
  return tcp_bind(slot, &ep);
}

int
net_tcp_user_listen(int slot)
{
  if(slot < 0)
    return -1;
  return tcp_listen(slot, 8);
}

int
net_tcp_user_accept(struct file *lf, struct file **out)
{
  int newsid;
  struct file *nf;

  if(out == 0 || lf == 0 || lf->type != FD_TCPSOCK)
    return -1;
  if(lf->tcp_slot < 0 || lf->tcp_listen == 0)
    return -1;

  newsid = tcp_accept(lf->tcp_slot, 0);
  if(newsid < 0)
    return -1;

  nf = filealloc();
  if(nf == 0){
    tcp_close(newsid);
    return -1;
  }
  nf->type = FD_TCPSOCK;
  nf->readable = 1;
  nf->writable = 1;
  nf->tcp_slot = newsid;
  nf->tcp_listen = 0;
  *out = nf;
  return 0;
}

int
net_tcp_user_connect(int slot, uchar rip[4], ushort rport)
{
  struct ip_endpoint ep;
  static int tcp_conn_dbg;

  if(slot < 0 || !net_get_microps_l3())
    return -1;
  ep.addr = 0;
  memmove(&ep.addr, rip, 4);
  ep.port = hton16(rport);
  if(tcp_conn_dbg < 8){
    cprintf("tcp_user_connect dbg: slot=%d ip=%d.%d.%d.%d addr=%x port=%d\n",
            slot, rip[0], rip[1], rip[2], rip[3], ep.addr, rport);
    tcp_conn_dbg++;
  }
  return tcp_connect(slot, &ep);
}

int
net_tcp_user_read(int slot, char *buf, int n, int listen_fd)
{
  if(slot < 0 || listen_fd)
    return -1;
  return (int)tcp_receive(slot, (uint8_t*)buf, (size_t)n);
}

int
net_tcp_user_write(int slot, char *buf, int n, int listen_fd)
{
  if(slot < 0 || listen_fd)
    return -1;
  return (int)tcp_send(slot, (uint8_t*)buf, (size_t)n);
}

int
net_tcp_user_fdready(int slot, int forwrite, int listen_fd)
{
  if(slot < 0)
    return -1;
  if(proc && proc->killed)
    return -1;
  /*
   * 在 DHCP 续租等路径临时切回 legacy L3 后，TCP 回包会被送到旧栈而不是 microps，
   * 造成 curl/connect 后永远无可读数据。TCP 用户路径统一强制回到 microps L3。
   */
  if(!net_get_microps_l3())
    net_set_microps_l3(1);
  if(net_is_available()){
    net_poll();
    net_protocol_handler();
    sirpair_microps_poll_timers();
  }
  if(listen_fd){
    if(forwrite)
      return 0;
    return tcp_sock_listen_readable(slot);
  }
  if(forwrite)
    return 1;
  return tcp_sock_has_rx_data(slot);
}

int
net_udp_user_alloc(void)
{
  if(!net_is_available() || !net_get_microps_l3())
    return -1;
  return sock_open(AF_INET, SOCK_DGRAM, 0);
}

void
net_udp_user_close(int slot)
{
  if(slot < 0)
    return;
  sock_close(slot);
}

void
net_udp_user_unref(int slot)
{
  net_udp_user_close(slot);
}

int
net_udp_user_bind(int slot, char *path)
{
  uchar ip[4];
  int port;
  struct sockaddr_in sa;

  if(slot < 0 || path == 0)
    return -1;
  if(parse_ipv4_port_bind(path, ip, &port) < 0)
    return -1;
  if(port < 0 || port > 65535)
    return -1;
  if(port == 0)
    port = 40300 + slot;
  ip4_to_sockaddr(&sa, ip, port);
  return sock_bind(slot, (struct sockaddr *)&sa, sizeof(sa));
}

int
net_udp_user_recvfrom(int slot, char *buf, int n, uchar src_ip[4], int *src_port)
{
  struct sockaddr_in peer;
  int plen;
  ssize_t r;
  ip_addr_t a;

  if(slot < 0 || n < 0 || buf == 0 || src_ip == 0 || src_port == 0)
    return -1;
  if(n == 0)
    return 0;
  plen = sizeof(peer);
  r = sock_recvfrom(slot, buf, (size_t)n, (struct sockaddr *)&peer, &plen);
  if(r < 0)
    return -1;
  a = peer.sin_addr;
  memmove(src_ip, &a, 4);
  *src_port = (int)ntoh16(peer.sin_port);
  return (int)r;
}

int
net_udp_user_sendto(int slot, char *buf, int n, uchar dst_ip[4], ushort dst_port)
{
  struct sockaddr_in sa;

  if(slot < 0 || n < 0 || buf == 0 || dst_ip == 0)
    return -1;
  if(n == 0)
    return 0;
  ip4_to_sockaddr(&sa, dst_ip, (int)dst_port);
  return (int)sock_sendto(slot, buf, (size_t)n, (struct sockaddr *)&sa, sizeof(sa));
}

int
net_udp_user_fdready(int slot, int forwrite)
{
  if(slot < 0)
    return -1;
  if(proc && proc->killed)
    return -1;
  return sock_fdready(slot, forwrite, 0);
}

int
net_dns_query(const char *name, uint *out_ip)
{
  uchar gw[4], mask[4], dns[4], myip[4];
  uchar qname[256];
  uchar dnsmsg[512];
  struct dns_hdr *h;
  int nlen, qlen, msglen;
  int sid;
  struct sockaddr_in peer;
  int plen;
  ssize_t nr;
  uint lease;
  int ok;
  int tries;
  uint start, now;
  ushort xid;

  if(!net_is_available() || name == 0 || out_ip == 0)
    return -1;
  if(!net_get_microps_l3())
    return -1;

  net_get_ipv4(myip);
  net_get_dhcp_cfg(gw, mask, dns, &lease, &ok);
  if(!ok || ip_is_zero(dns))
    return -1;

  nlen = dns_encode_name(name, qname, sizeof(qname));
  if(nlen < 0)
    return -1;
  qlen = (int)sizeof(*h) + nlen + 4;
  if(qlen > (int)sizeof(dnsmsg))
    return -1;

  for(tries = 0; tries < 2; tries++){
    memset(dnsmsg, 0, sizeof(dnsmsg));
    xid = (ushort)((ticks + tries) & 0xFFFF);
    h = (struct dns_hdr *)dnsmsg;
    h->id = hton16(xid);
    h->flags = hton16(0x0100);
    h->qdcount = hton16(1);
    memmove(dnsmsg + sizeof(*h), qname, (size_t)nlen);
    msglen = (int)sizeof(*h) + nlen;
    *(ushort *)(dnsmsg + msglen) = hton16(1);
    msglen += 2;
    *(ushort *)(dnsmsg + msglen) = hton16(1);
    msglen += 2;

    sid = sock_open(AF_INET, SOCK_DGRAM, 0);
    if(sid < 0)
      return -1;
    {
      struct sockaddr_in me;
      memset(&me, 0, sizeof(me));
      me.sin_family = AF_INET;
      me.sin_addr = IP_ADDR_ANY;
      me.sin_port = hton16((uint16_t)(53000 + (ticks & 0x3FF)));
      if(sock_bind(sid, (struct sockaddr *)&me, sizeof(me)) < 0){
        sock_close(sid);
        continue;
      }
    }
    {
      struct sockaddr_in to;
      int attempt;
      ip4_to_sockaddr(&to, dns, DNS_SERVER_PORT);
      for(attempt = 0; attempt < 48; attempt++){
        if(proc && proc->killed){
          sock_close(sid);
          return -1;
        }
        if(sock_sendto(sid, dnsmsg, (size_t)msglen, (struct sockaddr *)&to, sizeof(to)) >= 0)
          break;
        net_poll();
        sirpair_microps_poll_timers();
      }
      if(attempt >= 48){
        sock_close(sid);
        continue;
      }
    }

    acquire(&tickslock);
    start = ticks;
    release(&tickslock);
    for(;;){
      if(proc && proc->killed){
        sock_close(sid);
        return -1;
      }
      net_poll();
      sirpair_microps_poll_timers();
      if(sock_fdready(sid, 0, 0)){
        plen = sizeof(peer);
        nr = sock_recvfrom(sid, dnsmsg, sizeof(dnsmsg), (struct sockaddr *)&peer, &plen);
        if(nr > 0){
          ip_addr_t pa = peer.sin_addr;
          uchar sip[4];
          memmove(sip, &pa, 4);
          if(dns_parse_a_answer((uchar *)dnsmsg, (int)nr, xid, sip, myip, gw, mask, dns, out_ip) == 0){
            sock_close(sid);
            return 0;
          }
        }
      }
      acquire(&tickslock);
      now = ticks;
      release(&tickslock);
      if((int)(now - start) > 150)
        break;
    }
    sock_close(sid);
  }
  return -1;
}
