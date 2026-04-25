#ifndef MICROPS_SHIM_H
#define MICROPS_SHIM_H

#include "types.h"
#include "defs.h"

#define memcpy(d, s, n) memmove((d), (s), (uint)(n))

#endif
