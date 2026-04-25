#include "locale.h"

static char decpt[] = ".";
static struct lconv lc;

char *
setlocale(int category, const char *locale)
{
  (void)category;
  (void)locale;
  return "C";
}

struct lconv *
localeconv(void)
{
  lc.decimal_point = decpt;
  lc.thousands_sep = "";
  lc.grouping = "";
  return &lc;
}
