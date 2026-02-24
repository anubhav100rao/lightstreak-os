#ifndef STDINT_H
#define STDINT_H

/* Freestanding shim — re-exports from types.h */
#include "types.h"

#define UINT8_MAX   0xFF
#define UINT16_MAX  0xFFFF
#define UINT32_MAX  0xFFFFFFFFU
#define INT32_MIN   (-0x7FFFFFFF - 1)
#define INT32_MAX   0x7FFFFFFF

#endif /* STDINT_H */
