#ifndef USERLIB_STRING_H
#define USERLIB_STRING_H

/*
 * userspace/lib/string.h — Minimal string functions (no libc)
 */

typedef unsigned int size_t;

size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *s, int c, size_t n);
void   itoa(int value, char *buf, int base);
void   utoa(unsigned int value, char *buf, int base);

#endif /* USERLIB_STRING_H */
