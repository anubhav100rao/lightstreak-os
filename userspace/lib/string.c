/*
 * userspace/lib/string.c — Minimal string functions (no libc)
 */

#include "string.h"

size_t strlen(const char *s) {
  size_t len = 0;
  while (s[len])
    len++;
  return len;
}

int strcmp(const char *a, const char *b) {
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return (int)((unsigned char)*a - (unsigned char)*b);
}

int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i])
      return (int)((unsigned char)a[i] - (unsigned char)b[i]);
    if (a[i] == '\0')
      return 0;
  }
  return 0;
}

char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++))
    ;
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i]; i++)
    dst[i] = src[i];
  for (; i < n; i++)
    dst[i] = '\0';
  return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < n; i++)
    d[i] = s[i];
  return dst;
}

void *memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  for (size_t i = 0; i < n; i++)
    p[i] = (unsigned char)c;
  return s;
}

void utoa(unsigned int value, char *buf, int base) {
  char tmp[32];
  int i = 0;
  if (value == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  while (value > 0) {
    int d = value % base;
    tmp[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
    value /= base;
  }
  int j = 0;
  while (i-- > 0)
    buf[j++] = tmp[i];
  buf[j] = '\0';
}

void itoa(int value, char *buf, int base) {
  if (value < 0 && base == 10) {
    *buf++ = '-';
    utoa((unsigned int)(-(value + 1)) + 1, buf, base);
  } else {
    utoa((unsigned int)value, buf, base);
  }
}
