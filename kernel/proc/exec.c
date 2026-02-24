/*
 * kernel/proc/exec.c — Process loader
 *
 * Loads a flat binary from ramfs into memory at 0x600000 (above
 * the kernel heap) and creates a kernel thread that jumps to it.
 *
 * The binary is linked at 0x600000 by linker_user.ld and stripped
 * to a flat binary with objcopy.
 *
 * Memory layout:
 *   0x100000  — kernel code/data
 *   0x313000  — heap (2 MB)
 *   0x513000  — heap end
 *   0x600000  — user binary (loaded here)  ← MUST be above heap
 *   0x800000  — end of identity-mapped region
 */

#include "exec.h"
#include "../fs/ramfs.h"
#include "../fs/vfs.h"
#include "../kernel.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "process.h"
#include "scheduler.h"

#define USER_LOAD_ADDR 0x600000u /* Must match linker_user.ld */
#define PAGE_SIZE 4096u

static void mem_copy_exec(void *dst, const void *src, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (uint32_t i = 0; i < n; i++)
    d[i] = s[i];
}

/*
 * Shell entry trampoline — kernel thread that jumps to the loaded binary.
 */
static void shell_trampoline(void) {
  void (*entry)(void) = (void (*)(void))USER_LOAD_ADDR;
  entry();
  /* If the shell ever returns, exit gracefully */
  kprintf("[EXEC] Shell returned, halting\n");
  khalt();
}

process_t *exec(const char *filename) {
  /* 1. Open the file in ramfs */
  int file_idx = vfs_open(filename);
  if (file_idx < 0) {
    kprintf("[EXEC] File not found: %s\n", filename);
    return NULL;
  }

  /* Read file into temp buffer */
  uint8_t *tmpbuf = (uint8_t *)kmalloc(32 * 1024);
  if (!tmpbuf) {
    kprintf("[EXEC] Out of memory for temp buffer\n");
    return NULL;
  }
  int32_t file_size = vfs_read(file_idx, tmpbuf, 0, 32 * 1024);
  if (file_size <= 0) {
    kprintf("[EXEC] Empty or unreadable file: %s\n", filename);
    kfree(tmpbuf);
    return NULL;
  }

  /* 2. Copy binary directly to 0x600000.
   *    This address is within the 0-8MB identity map (virtual = physical)
   *    and above the heap end (~0x513000), so no conflict. */
  mem_copy_exec((void *)USER_LOAD_ADDR, tmpbuf, (uint32_t)file_size);
  kfree(tmpbuf);

  /* 3. Create as a kernel thread using shell_trampoline */
  process_t *p = process_create(shell_trampoline);
  if (!p) {
    kprintf("[EXEC] Failed to create process\n");
    return NULL;
  }

  /* Initialise fd_table: fd 0 = stdin, fd 1 = stdout, fd 2 = stderr */
  for (int i = 0; i < MAX_FDS; i++) {
    p->fd_table[i].file_idx = -1;
    p->fd_table[i].offset = 0;
    p->fd_table[i].in_use = 0;
  }
  p->fd_table[0].in_use = 1;
  p->fd_table[0].file_idx = -1;
  p->fd_table[1].in_use = 1;
  p->fd_table[1].file_idx = -1;
  p->fd_table[2].in_use = 1;
  p->fd_table[2].file_idx = -1;

  scheduler_add(p);

  kprintf("[EXEC] Loaded '%s' (%d bytes) as PID %u at 0x%x\n", filename,
          file_size, p->pid, USER_LOAD_ADDR);
  return p;
}
