#ifndef STDDEF_H
#define STDDEF_H

/* Freestanding shim */
#include "types.h"

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* STDDEF_H */
