#ifndef SYSCALL_TABLE_H
#define SYSCALL_TABLE_H

/*
 * kernel/syscall/syscall_table.h — System call number definitions
 *
 * Convention (Linux-inspired):
 *   User program executes: int 0x80
 *   EAX = syscall number
 *   EBX, ECX, EDX = arguments (up to 3)
 *   Return value placed in EAX
 */

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

#define SYS_MAX      10   /* Highest valid syscall number */

#endif /* SYSCALL_TABLE_H */
