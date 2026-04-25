#include "types.h"
#include "user.h"
#include "net.h"

static int
parse_ip(const char *s, uchar out[4])
{
  int i, v, ndig;
  const char *p = s;

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
    out[i] = (uchar)v;
    if(i < 3){
      if(*p != '.')
        return -1;
      p++;
    }
  }
  if(*p != 0)
    return -1;
  return 0;
}

int
main(int argc, char **argv)
{
  uchar ip[4];
  uint packed;
  int count = 4;
  int ok;
  int i;

  if(argc > 3){
    printf(2, "usage: ping [ip] [count]\n");
    exit(0);
  }

  if(argc >= 2){
    if(parse_ip(argv[1], ip) < 0){
      printf(2, "ping: bad ip %s\n", argv[1]);
      exit(0);
    }
  } else {
    struct net_cfg cfg;
    if(getnetcfg(&cfg) < 0){
      printf(2, "ping: getnetcfg failed\n");
      exit(0);
    }
    for(i = 0; i < 4; i++)
      ip[i] = cfg.gw[i];
  }

  if(argc == 3){
    count = atoi(argv[2]);
    if(count <= 0)
      count = 4;
  }

  packed = ((uint)ip[0] << 24) | ((uint)ip[1] << 16) | ((uint)ip[2] << 8) | (uint)ip[3];
  ok = ping(packed, count);
  if(ok < 0){
    printf(2, "ping: failed\n");
    exit(0);
  }

  printf(1, "ping %d.%d.%d.%d: tx=%d rx=%d lost=%d\n",
         ip[0], ip[1], ip[2], ip[3], count, ok, count - ok);
  exit(0);
}
