/*
 * kernel/syscall/syscall.c — System call dispatcher
 *
 * Handles int 0x80 by reading the syscall number from EAX and dispatching
 * to the appropriate handler.  Arguments are passed in EBX, ECX, EDX.
 * The return value is placed back into regs->eax so userspace sees it.
 */

#include "syscall.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/vga.h"
#include "../fs/vfs.h"
#include "../kernel.h"
#include "../mm/pmm.h"
#include "../proc/process.h"
#include "../proc/scheduler.h"
#include "syscall_table.h"

/* -------------------------------------------------------------------------
 * sys_write — write bytes to a file descriptor
 * ---------------------------------------------------------------------- */
static int32_t sys_write(uint32_t fd, const char *buf, uint32_t len) {
  if (fd == 1 || fd == 2) {
    /* stdout / stderr → VGA */
    for (uint32_t i = 0; i < len; i++) {
      vga_putchar(buf[i]);
    }
    return (int32_t)len;
  }

  /* Regular file fd → VFS */
  process_t *cur = scheduler_current();
  if (!cur || fd >= MAX_FDS || !cur->fd_table[fd].in_use)
    return -1;
  int file_idx = cur->fd_table[fd].file_idx;
  if (file_idx < 0)
    return -1;
  int32_t written = vfs_write(file_idx, buf, cur->fd_table[fd].offset, len);
  if (written > 0)
    cur->fd_table[fd].offset += (uint32_t)written;
  return written;
}

/* -------------------------------------------------------------------------
 * sys_read — read bytes from a file descriptor
 * ---------------------------------------------------------------------- */
static int32_t sys_read(uint32_t fd, char *buf, uint32_t len) {
  if (fd == 0) {
    /* stdin → keyboard */
    for (uint32_t i = 0; i < len; i++) {
      buf[i] = keyboard_getchar();
    }
    return (int32_t)len;
  }

  /* Regular file fd → VFS */
  process_t *cur = scheduler_current();
  if (!cur || fd >= MAX_FDS || !cur->fd_table[fd].in_use)
    return -1;
  int file_idx = cur->fd_table[fd].file_idx;
  if (file_idx < 0)
    return -1;
  int32_t bytes = vfs_read(file_idx, buf, cur->fd_table[fd].offset, len);
  if (bytes > 0)
    cur->fd_table[fd].offset += (uint32_t)bytes;
  return bytes;
}

/* -------------------------------------------------------------------------
 * sys_open — open a file and return a file descriptor
 * ---------------------------------------------------------------------- */
static int32_t sys_open(const char *path) {
  process_t *cur = scheduler_current();
  if (!cur)
    return -1;

  /* Find a free fd slot (skip 0/1/2 — reserved for stdio) */
  int fd = -1;
  for (int i = 3; i < MAX_FDS; i++) {
    if (!cur->fd_table[i].in_use) {
      fd = i;
      break;
    }
  }
  if (fd < 0)
    return -1; /* No free fd */

  int file_idx = vfs_open(path);
  if (file_idx < 0)
    return -1;

  cur->fd_table[fd].file_idx = file_idx;
  cur->fd_table[fd].offset = 0;
  cur->fd_table[fd].in_use = 1;
  return fd;
}

/* -------------------------------------------------------------------------
 * sys_close — close a file descriptor
 * ---------------------------------------------------------------------- */
static int32_t sys_close(uint32_t fd) {
  process_t *cur = scheduler_current();
  if (!cur || fd < 3 || fd >= MAX_FDS)
    return -1;
  if (!cur->fd_table[fd].in_use)
    return -1;

  vfs_close(cur->fd_table[fd].file_idx);
  cur->fd_table[fd].in_use = 0;
  cur->fd_table[fd].file_idx = -1;
  cur->fd_table[fd].offset = 0;
  return 0;
}

/* -------------------------------------------------------------------------
 * sys_exit — terminate the current process
 * ---------------------------------------------------------------------- */
static void sys_exit(int32_t code) {
  process_t *cur = scheduler_current();
  if (cur) {
    cur->state = PROC_ZOMBIE;
    cur->exit_code = code;
    /* Do NOT call scheduler_remove() here — it sets cur->next = NULL, which
     * causes a NULL dereference in scheduler_tick() below when it does
     * next = current->next.  Leaving the zombie in the ring is safe: the
     * scheduler's READY-only loop will skip it on every subsequent tick. */
  }
  scheduler_tick((registers_t *)0);
  khalt();
}

/* -------------------------------------------------------------------------
 * sys_getpid — return the PID of the calling process
 * ---------------------------------------------------------------------- */
static uint32_t sys_getpid(void) {
  process_t *cur = scheduler_current();
  return cur ? cur->pid : 0;
}

/* -------------------------------------------------------------------------
 * sys_readdir — list files in the root directory
 * ---------------------------------------------------------------------- */
static int32_t sys_readdir(dirent_t *entries, int max) {
  return vfs_readdir(entries, max);
}

/* -------------------------------------------------------------------------
 * sys_uptime — return seconds since boot
 * ---------------------------------------------------------------------- */
static uint32_t sys_uptime(void) { return timer_get_seconds(); }

/* -------------------------------------------------------------------------
 * Meminfo / PS data structures (shared with userspace)
 * ---------------------------------------------------------------------- */
typedef struct {
  uint32_t total_pages;
  uint32_t free_pages;
  uint32_t used_pages;
} meminfo_t;

typedef struct {
  uint32_t pid;
  uint32_t state;
} ps_entry_t;

/* -------------------------------------------------------------------------
 * sys_meminfo — return physical memory statistics
 * ---------------------------------------------------------------------- */
static int32_t sys_meminfo(meminfo_t *info) {
  if (!info)
    return -1;
  info->total_pages = pmm_get_total_pages();
  info->free_pages = pmm_get_free_pages();
  info->used_pages = pmm_get_used_pages();
  return 0;
}

/* -------------------------------------------------------------------------
 * sys_ps — list running processes
 * ---------------------------------------------------------------------- */
static int32_t sys_ps(ps_entry_t *entries, int max) {
  if (!entries || max <= 0)
    return -1;

  /* Walk the scheduler's run queue.
   * We access scheduler_current() and iterate via ->next. */
  process_t *cur = scheduler_current();
  if (!cur)
    return 0;

  int count = 0;
  process_t *p = cur;
  do {
    if (count < max) {
      entries[count].pid = p->pid;
      entries[count].state = (uint32_t)p->state;
      count++;
    }
    p = p->next;
  } while (p && p != cur && count < max);

  return count;
}

/* -------------------------------------------------------------------------
 * syscall_handler — main dispatcher
 * ---------------------------------------------------------------------- */
void syscall_handler(registers_t *regs) {
  uint32_t num = regs->eax;

  switch (num) {
  case SYS_WRITE:
    regs->eax =
        (uint32_t)sys_write(regs->ebx, (const char *)regs->ecx, regs->edx);
    break;

  case SYS_READ:
    regs->eax = (uint32_t)sys_read(regs->ebx, (char *)regs->ecx, regs->edx);
    break;

  case SYS_OPEN:
    regs->eax = (uint32_t)sys_open((const char *)regs->ebx);
    break;

  case SYS_CLOSE:
    regs->eax = (uint32_t)sys_close(regs->ebx);
    break;

  case SYS_EXIT:
    sys_exit((int32_t)regs->ebx);
    break;

  case SYS_GETPID:
    regs->eax = sys_getpid();
    break;

  case SYS_READDIR:
    regs->eax = (uint32_t)sys_readdir((dirent_t *)regs->ebx, (int)regs->ecx);
    break;

  case SYS_UPTIME:
    regs->eax = sys_uptime();
    break;

  case SYS_MEMINFO:
    regs->eax = (uint32_t)sys_meminfo((meminfo_t *)regs->ebx);
    break;

  case SYS_PS:
    regs->eax = (uint32_t)sys_ps((ps_entry_t *)regs->ebx, (int)regs->ecx);
    break;

  default:
    regs->eax = (uint32_t)-1;
    break;
  }
}

/* -------------------------------------------------------------------------
 * syscall_init
 * ---------------------------------------------------------------------- */
void syscall_init(void) {
  kprintf("[SYSCALL] System call interface ready (int 0x80, %d syscalls)\n",
          SYS_MAX);
  debug_print("[serial] Syscall init\n");
}
