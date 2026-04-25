#include "signal.h"

sighandler_t
signal(int sig, sighandler_t handler)
{
  (void)sig;
  (void)handler;
  return SIG_DFL;
}
