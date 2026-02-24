#ifndef USERLIB_SYSCALL_H
#define USERLIB_SYSCALL_H

/*
 * userspace/lib/syscall_wrappers.h — Thin wrappers over int 0x80
 *
 * These are the ONLY way userspace code can request kernel services.
 * No libc — everything goes through these inline asm wrappers.
 */

/* Syscall numbers — must match kernel/syscall/syscall_table.h */
#define SYS_EXIT     1
#define SYS_WRITE    2
#define SYS_READ     3
#define SYS_OPEN     4
#define SYS_CLOSE    5
#define SYS_GETPID   6
#define SYS_READDIR  7
#define SYS_UPTIME   8
#define SYS_MEMINFO  9
#define SYS_PS       10

typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
typedef int            int32_t;
typedef unsigned int   size_t;

#define NULL ((void *)0)

static inline void sys_exit(int code) {
    __asm__ volatile("int $0x80" :: "a"(SYS_EXIT), "b"(code));
}

static inline int sys_write(int fd, const void *buf, size_t len) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}

static inline int sys_read(int fd, void *buf, size_t len) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_READ), "b"(fd), "c"(buf), "d"(len)
        : "memory");
    return ret;
}

static inline int sys_open(const char *path) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_OPEN), "b"(path)
        : "memory");
    return ret;
}

static inline int sys_close(int fd) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_CLOSE), "b"(fd));
    return ret;
}

static inline int sys_getpid(void) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETPID));
    return ret;
}

static inline int sys_readdir(void *entries, int max) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_READDIR), "b"(entries), "c"(max)
        : "memory");
    return ret;
}

static inline uint32_t sys_uptime(void) {
    uint32_t ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_UPTIME));
    return ret;
}

/* meminfo struct layout — must match kernel side */
typedef struct {
    uint32_t total_pages;
    uint32_t free_pages;
    uint32_t used_pages;
} meminfo_t;

static inline int sys_meminfo(meminfo_t *info) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_MEMINFO), "b"(info)
        : "memory");
    return ret;
}

/* ps entry — must match kernel side */
typedef struct {
    uint32_t pid;
    uint32_t state;
} ps_entry_t;

static inline int sys_ps(ps_entry_t *entries, int max) {
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(SYS_PS), "b"(entries), "c"(max)
        : "memory");
    return ret;
}

#endif /* USERLIB_SYSCALL_H */
