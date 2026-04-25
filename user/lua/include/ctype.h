#ifndef SIRPAIR_CTYPE_H
#define SIRPAIR_CTYPE_H

static inline int
sirpair_uc(int c)
{
  return (int)(unsigned char)c;
}

static inline int
isspace(int c)
{
  unsigned char u = (unsigned char)c;
  return u == ' ' || u == '\f' || u == '\n' || u == '\r' || u == '\t' || u == '\v';
}

static inline int
isdigit(int c)
{
  unsigned char u = (unsigned char)c;
  return u >= '0' && u <= '9';
}

static inline int
isxdigit(int c)
{
  unsigned char u = (unsigned char)c;
  return isdigit(c) || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

static inline int
isalpha(int c)
{
  unsigned char u = (unsigned char)c;
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z');
}

static inline int
isalnum(int c)
{
  return isalpha(c) || isdigit(c);
}

static inline int
isupper(int c)
{
  unsigned char u = (unsigned char)c;
  return u >= 'A' && u <= 'Z';
}

static inline int
islower(int c)
{
  unsigned char u = (unsigned char)c;
  return u >= 'a' && u <= 'z';
}

static inline int
toupper(int c)
{
  if(islower(c))
    return c - ('a' - 'A');
  return c;
}

static inline int
tolower(int c)
{
  if(isupper(c))
    return c + ('a' - 'A');
  return c;
}

static inline int
isprint(int c)
{
  unsigned char u = (unsigned char)c;
  return u >= 0x20 && u <= 0x7e;
}

static inline int
iscntrl(int c)
{
  unsigned char u = (unsigned char)c;
  return u < 0x20 || u == 0x7f;
}

static inline int
isgraph(int c)
{
  unsigned char u = (unsigned char)c;
  return u > 0x20 && u <= 0x7e;
}

static inline int
ispunct(int c)
{
  return isgraph(c) && !isalnum(c);
}

#endif
