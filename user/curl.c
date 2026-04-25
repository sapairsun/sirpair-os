#include "types.h"
#include "user.h"
#include "usock.h"
#include "fcntl.h"

#define CURL_HDR_MAX 16
#define CURL_HOST_MAX 128
#define CURL_PATH_MAX 256
#define CURL_ADDR_MAX 64
#define CURL_REQ_MAX 768
#define CURL_IOBUF_MAX 1024
#define CURL_HEADBUF_MAX 8192
#define CURL_SEND_TIMEOUT_TICKS 1200
#define CURL_FIRST_BYTE_TIMEOUT_TICKS 8000
#define CURL_IDLE_TIMEOUT_TICKS 2000
#define CURL_RETRY_ATTEMPTS 3

struct curl_opt {
  char *url;
  char *method;
  char *data;
  char *outfile;
  char *headers[CURL_HDR_MAX];
  int nheaders;
  int include_headers;
  int head_only;
};

// 避免用户栈溢出：xv6/Sirpair 用户栈很小，使用静态缓冲区。
static char g_host[CURL_HOST_MAX];
static char g_path[CURL_PATH_MAX];
static char g_addr[CURL_ADDR_MAX];
static char g_req[CURL_REQ_MAX];
static char g_iobuf[CURL_IOBUF_MAX];
static char g_headbuf[CURL_HEADBUF_MAX];

static int
is_digit(char c)
{
  return c >= '0' && c <= '9';
}

static int
starts_with(const char *s, const char *prefix)
{
  int i;
  for(i = 0; prefix[i]; i++){
    if(s[i] != prefix[i])
      return 0;
  }
  return 1;
}

static int
parse_port(const char *s, int *out)
{
  int i, v;
  if(!s || !out || s[0] == 0)
    return -1;
  v = 0;
  for(i = 0; s[i]; i++){
    if(!is_digit(s[i]))
      return -1;
    v = v * 10 + (s[i] - '0');
    if(v > 65535)
      return -1;
  }
  if(v <= 0)
    return -1;
  *out = v;
  return 0;
}

static int
parse_ipv4(const char *s, uchar ip[4])
{
  int i, n, v;
  const char *p;
  if(!s || !ip)
    return -1;
  p = s;
  for(i = 0; i < 4; i++){
    n = 0;
    v = 0;
    while(is_digit(*p)){
      v = v * 10 + (*p - '0');
      if(v > 255)
        return -1;
      p++;
      n++;
    }
    if(n == 0)
      return -1;
    ip[i] = (uchar)v;
    if(i < 3){
      if(*p != '.')
        return -1;
      p++;
    }
  }
  return *p == 0 ? 0 : -1;
}

static void
u32_to_dec(int v, char *out)
{
  char rev[16];
  int k, t, i;
  k = 0;
  t = v;
  if(t == 0)
    rev[k++] = '0';
  else
    while(t > 0 && k < 15){
      rev[k++] = '0' + (t % 10);
      t /= 10;
    }
  i = 0;
  while(k > 0)
    out[i++] = rev[--k];
  out[i] = 0;
}

static void
append_cstr(char *dst, const char *src)
{
  int p, i;
  p = strlen(dst);
  for(i = 0; src[i]; i++)
    dst[p++] = src[i];
  dst[p] = 0;
}

static void
format_ipv4(uint ip, char out[16])
{
  char a[16], b[16], c[16], d[16];
  u32_to_dec((ip >> 24) & 0xFF, a);
  u32_to_dec((ip >> 16) & 0xFF, b);
  u32_to_dec((ip >> 8) & 0xFF, c);
  u32_to_dec(ip & 0xFF, d);
  strcpy(out, a);
  append_cstr(out, ".");
  append_cstr(out, b);
  append_cstr(out, ".");
  append_cstr(out, c);
  append_cstr(out, ".");
  append_cstr(out, d);
}

static int
build_addr(const char *host, int port, char out[CURL_ADDR_MAX])
{
  uchar ip4[4];
  uint dip;
  char ipstr[16];
  char pbuf[16];
  if(!host || !out)
    return -1;
  if(parse_ipv4(host, ip4) == 0){
    strcpy(ipstr, host);
  } else {
    if(dig((char*)host, &dip) < 0)
      return -1;
    format_ipv4(dip, ipstr);
  }
  u32_to_dec(port, pbuf);
  if(strlen(ipstr) + 1 + strlen(pbuf) + 1 > CURL_ADDR_MAX)
    return -1;
  strcpy(out, ipstr);
  append_cstr(out, ":");
  append_cstr(out, pbuf);
  return 0;
}

static int
parse_url(const char *url, char host[CURL_HOST_MAX], int *port, char path[CURL_PATH_MAX])
{
  const char *p, *slash, *colon;
  int i, hlen, pnum;

  if(!url || !host || !port || !path)
    return -1;

  p = url;
  if(starts_with(p, "http://")){
    p += 7;
  } else if(starts_with(p, "https://")){
    return -2;
  } else {
    for(i = 0; p[i]; i++){
      if(p[i] == ':' && p[i+1] == '/' && p[i+2] == '/')
        return -3;
    }
  }

  slash = 0;
  for(i = 0; p[i]; i++){
    if(p[i] == '/'){
      slash = p + i;
      break;
    }
  }

  if(slash){
    hlen = slash - p;
    if(hlen <= 0 || hlen >= CURL_HOST_MAX)
      return -1;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    if(strlen(slash) >= CURL_PATH_MAX)
      return -1;
    strcpy(path, slash);
  } else {
    if(strlen(p) == 0 || strlen(p) >= CURL_HOST_MAX)
      return -1;
    strcpy(host, p);
    strcpy(path, "/");
  }

  *port = 80;
  colon = 0;
  for(i = 0; host[i]; i++){
    if(host[i] == ':')
      colon = host + i;
  }
  if(colon){
    hlen = colon - host;
    if(hlen <= 0 || hlen >= CURL_HOST_MAX)
      return -1;
    if(parse_port(colon + 1, &pnum) < 0)
      return -1;
    host[hlen] = 0;
    *port = pnum;
  }
  return 0;
}

static int
send_all(int fd, const char *buf, int n)
{
  int off, r;
  uint start;
  off = 0;
  start = uptime();
  while(off < n){
    r = send(fd, (void*)(buf + off), n - off);
    if(r > 0){
      off += r;
      continue;
    }
    if(r < 0)
      return -1;
    if((uint)(uptime() - start) > CURL_SEND_TIMEOUT_TICKS)
      return -1;
    if(sleep(1) < 0)
      return -1;
  }
  return 0;
}

static int
write_all(int fd, const char *buf, int n)
{
  int off, r;
  off = 0;
  while(off < n){
    r = write(fd, (void*)(buf + off), n - off);
    if(r <= 0)
      return -1;
    off += r;
  }
  return 0;
}

static int
find_head_end(const char *buf, int n)
{
  int i;
  for(i = 0; i + 3 < n; i++){
    if(buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n')
      return i + 4;
  }
  for(i = 0; i + 1 < n; i++){
    if(buf[i] == '\n' && buf[i+1] == '\n')
      return i + 2;
  }
  return -1;
}

static int
append_req(const char *s, int *pos)
{
  int i, p;
  p = *pos;
  for(i = 0; s[i]; i++){
    if(p >= CURL_REQ_MAX - 1)
      return -1;
    g_req[p++] = s[i];
  }
  *pos = p;
  return 0;
}

static void
usage(void)
{
  printf(2,
    "用法: curl [选项] 网址\n"
    "  -o <文件>   输出到文件\n"
    "  -I          仅输出响应头\n"
    "  -i          输出响应头和响应体\n"
    "  -X <方法>   指定请求方法\n"
    "  -d <数据>   发送请求体(默认改为 POST)\n"
    "  -H <请求头> 追加请求头(可重复)\n"
    "  -s          静默参数(兼容)\n"
    "仅支持 http，不支持 https。\n");
}

static int
parse_args(int argc, char **argv, struct curl_opt *opt)
{
  int i;
  memset(opt, 0, sizeof(*opt));
  for(i = 1; i < argc; i++){
    char *a = argv[i];
    if(strcmp(a, "--help") == 0){
      usage();
      exit(0);
    } else if(strcmp(a, "-o") == 0){
      if(i + 1 >= argc) return -1;
      opt->outfile = argv[++i];
    } else if(strcmp(a, "-I") == 0){
      opt->head_only = 0;
      opt->include_headers = 1;
      // 为提升兼容性，使用 GET 后在本地截断正文。
      opt->method = "GET";
    } else if(strcmp(a, "-i") == 0){
      opt->include_headers = 1;
    } else if(strcmp(a, "-s") == 0){
      // 不显示进度，保持兼容。
    } else if(strcmp(a, "-X") == 0){
      if(i + 1 >= argc) return -1;
      opt->method = argv[++i];
    } else if(strcmp(a, "-d") == 0 || strcmp(a, "--data") == 0){
      if(i + 1 >= argc) return -1;
      opt->data = argv[++i];
      if(!opt->method)
        opt->method = "POST";
    } else if(strcmp(a, "-H") == 0){
      if(i + 1 >= argc) return -1;
      if(opt->nheaders >= CURL_HDR_MAX) return -1;
      opt->headers[opt->nheaders++] = argv[++i];
    } else if(a[0] == '-'){
      return -1;
    } else {
      opt->url = a;
    }
  }
  if(!opt->url)
    return -1;
  if(!opt->method)
    opt->method = "GET";
  return 0;
}

int
main(int argc, char **argv)
{
  struct curl_opt opt;
  int prc, port, fd, outfd;
  int reqn, i, n, hlen, hend, body_only, got_data, attempt, rdy;
  char numbuf[16];
  uint wait_start;

  if(parse_args(argc, argv, &opt) < 0){
    usage();
    exit(1);
  }

  prc = parse_url(opt.url, g_host, &port, g_path);
  if(prc == -2){
    printf(2, "curl: 暂不支持 https\n");
    exit(1);
  }
  if(prc != 0){
    printf(2, "curl: 网址格式不支持: %s\n", opt.url);
    exit(1);
  }
  if(build_addr(g_host, port, g_addr) < 0){
    printf(2, "curl: 主机解析失败: %s\n", g_host);
    exit(1);
  }

  reqn = 0;
  if(append_req(opt.method, &reqn) < 0 ||
     append_req(" ", &reqn) < 0 ||
     append_req(g_path, &reqn) < 0 ||
     append_req(" HTTP/1.0\r\n", &reqn) < 0 ||
     append_req("Host: ", &reqn) < 0 ||
     append_req(g_host, &reqn) < 0 ||
     append_req("\r\nUser-Agent: curl\r\nAccept: */*\r\nConnection: close\r\n", &reqn) < 0){
    printf(2, "curl: 请求过长\n");
    exit(1);
  }

  if(opt.data){
    if(append_req("Content-Length: ", &reqn) < 0){
      exit(1);
    }
    u32_to_dec(strlen(opt.data), numbuf);
    if(append_req(numbuf, &reqn) < 0 ||
       append_req("\r\nContent-Type: application/x-www-form-urlencoded\r\n", &reqn) < 0){
      exit(1);
    }
  }
  for(i = 0; i < opt.nheaders; i++){
    if(append_req(opt.headers[i], &reqn) < 0 || append_req("\r\n", &reqn) < 0){
      exit(1);
    }
  }
  if(append_req("\r\n", &reqn) < 0){
    exit(1);
  }
  g_req[reqn] = 0;

  for(attempt = 0; attempt < CURL_RETRY_ATTEMPTS; attempt++){
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0){
      printf(2, "curl: 建立套接字失败\n");
      exit(1);
    }
    if(connect(fd, g_addr) < 0){
      close(fd);
      if(attempt == CURL_RETRY_ATTEMPTS - 1){
        printf(2, "curl: 连接失败: %s\n", g_addr);
        exit(1);
      }
      if(sleep(5) < 0)
        exit(130);
      continue;
    }
    if(send_all(fd, g_req, reqn) < 0){
      close(fd);
      if(attempt == CURL_RETRY_ATTEMPTS - 1){
        printf(2, "curl: 发送请求失败\n");
        exit(1);
      }
      if(sleep(5) < 0)
        exit(130);
      continue;
    }
    if(opt.data && send_all(fd, opt.data, strlen(opt.data)) < 0){
      close(fd);
      if(attempt == CURL_RETRY_ATTEMPTS - 1){
        printf(2, "curl: 发送请求体失败\n");
        exit(1);
      }
      if(sleep(5) < 0)
        exit(130);
      continue;
    }

    if(opt.outfile){
      unlink(opt.outfile);
      outfd = open(opt.outfile, O_CREATE | O_WRONLY | O_TRUNC);
      if(outfd < 0){
        printf(2, "curl: 打开输出文件失败: %s\n", opt.outfile);
        close(fd);
        exit(1);
      }
    } else {
      outfd = 1;
    }

    body_only = !opt.include_headers;
    hlen = 0;
    got_data = 0;
    wait_start = uptime();
    for(;;){
      rdy = fdready(fd, 0);
      if(rdy < 0){
        close(fd);
        if(outfd != 1) close(outfd);
        exit(130);
      }
      if(rdy == 0){
        uint limit;
        if(got_data)
          limit = CURL_IDLE_TIMEOUT_TICKS;
        else
          limit = CURL_FIRST_BYTE_TIMEOUT_TICKS;
        if((uint)(uptime() - wait_start) > limit)
          break;
        if(sleep(1) < 0){
          close(fd);
          if(outfd != 1) close(outfd);
          exit(130);
        }
        continue;
      }
      n = recv(fd, g_iobuf, sizeof(g_iobuf));
      if(n <= 0)
        break;
      got_data = 1;
      wait_start = uptime();
      if((body_only || opt.head_only) && hlen >= 0){
        if(hlen + n > CURL_HEADBUF_MAX){
          printf(2, "curl: 响应头过大\n");
          close(fd);
          if(outfd != 1) close(outfd);
          exit(1);
        }
        memcpy(g_headbuf + hlen, g_iobuf, n);
        hlen += n;
        hend = find_head_end(g_headbuf, hlen);
        if(hend < 0)
          continue;
        if(opt.head_only){
          if(write_all(outfd, g_headbuf, hend) < 0){
            close(fd);
            if(outfd != 1) close(outfd);
            exit(1);
          }
          break;
        } else if(body_only && hlen > hend){
          if(write_all(outfd, g_headbuf + hend, hlen - hend) < 0){
            close(fd);
            if(outfd != 1) close(outfd);
            exit(1);
          }
        }
        hlen = -1;
        continue;
      }
      if(opt.head_only)
        continue;
      if(write_all(outfd, g_iobuf, n) < 0){
        close(fd);
        if(outfd != 1) close(outfd);
        exit(1);
      }
    }

    close(fd);
    if(outfd != 1)
      close(outfd);
    if(got_data)
      exit(0);
    if(attempt < CURL_RETRY_ATTEMPTS - 1)
      if(sleep(5) < 0)
        exit(130);
  }
  printf(2, "curl: 请求超时，无响应数据\n");
  exit(1);
}
