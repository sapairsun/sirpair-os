#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void
free(void *ap)
{
  Header *bp, *p;

  if(ap == 0)
    return;
  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)
    nu = 4096;
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

void*
malloc(size_t nbytes)
{
  Header *p, *prevp;
  uint nunits;
  uint nb;

  if(nbytes > (size_t)0x7fffffffu)
    return 0;
  nb = (uint)nbytes;
  nunits = (nb + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        prevp->s.ptr = p->s.ptr;
      else {
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void*)(p + 1);
    }
    if(p == freep)
      if((p = morecore(nunits)) == 0)
        return 0;
  }
}

void*
calloc(size_t n, size_t m)
{
  size_t t;
  void *p;
  uint nb;

  if(n == 0 || m == 0)
    return malloc(1);
  t = n * m;
  if(m != 0 && t / m != n)
    return 0;
  nb = (uint)t;
  p = malloc(nb);
  if(p)
    memset(p, 0, nb);
  return p;
}

void*
realloc(void *ap, size_t nbytes)
{
  Header *bp;
  uint nunits;
  uint oldbytes;
  void *np;
  uint i;
  uint nb;
  char *src, *dst;

  if(nbytes == 0){
    free(ap);
    return 0;
  }
  if(ap == 0)
    return malloc((uint)nbytes);
  if(nbytes > (size_t)0x7fffffffu)
    return 0;
  nb = (uint)nbytes;

  bp = (Header*)ap - 1;
  oldbytes = (bp->s.size - 1) * (uint)sizeof(Header);
  nunits = (nb + sizeof(Header) - 1)/sizeof(Header) + 1;
  if(bp->s.size >= nunits)
    return ap;

  np = malloc(nb);
  if(np == 0)
    return 0;
  src = (char*)ap;
  dst = (char*)np;
  for(i = 0; i < oldbytes && i < nb; i++)
    dst[i] = src[i];
  free(ap);
  return np;
}
