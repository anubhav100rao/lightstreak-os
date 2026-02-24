#ifndef TYPES_H
#define TYPES_H

/* Fixed-width integer types — no stdlib */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

typedef uint32_t            size_t;
typedef int32_t             ssize_t;
typedef uint32_t            uintptr_t;

#define NULL  ((void *)0)
#define TRUE  1
#define FALSE 0

#define UNUSED(x) ((void)(x))

/* Commonly used attribute macros */
#define PACKED      __attribute__((packed))
#define NORETURN    __attribute__((noreturn))
#define ALIGNED(x)  __attribute__((aligned(x)))

#endif /* TYPES_H */
