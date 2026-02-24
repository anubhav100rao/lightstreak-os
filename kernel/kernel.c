/*
 * kernel/kernel.c — Kernel entry point + kprintf
 *
 * kmain() is the first C function called from boot.asm.
 * kprintf() is a minimal printf (no heap needed — uses the stack only).
 *
 * Supported format specifiers: %s %d %u %x %X %c %p %%
 */

#include "kernel.h"
#include "../include/multiboot.h"
#include "../include/types.h"
#include "arch/gdt.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "arch/irq.h"
#include "arch/tss.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/vga.h"
#include "fs/ramfs.h"
#include "fs/vfs.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/exec.h"
#include "proc/process.h"
#include "proc/scheduler.h"
#include "syscall/syscall.h"
#include "syscall/syscall_table.h"

/* Multiboot magic GRUB puts in EAX */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Exported by boot.asm / linker.ld */
extern uint8_t stack_top[];
extern uint8_t kernel_end[];

/* -------------------------------------------------------------------------
 * Serial debug output — port 0xE9 QEMU extension (-debugcon stdio)
 * ---------------------------------------------------------------------- */
void debug_print(const char *str) {
  while (*str) {
    outb(0xE9, (uint8_t)*str++);
  }
}

/* -------------------------------------------------------------------------
 * khalt — disable interrupts and loop forever
 * ---------------------------------------------------------------------- */
void NORETURN khalt(void) {
  for (;;) {
    __asm__ volatile("cli; hlt");
  }
}

/* -------------------------------------------------------------------------
 * kprintf — minimal formatted output to VGA
 * ---------------------------------------------------------------------- */
static void print_uint(uint32_t n, uint32_t base) {
  static const char digits[] = "0123456789abcdef";
  char buf[32];
  int i = 0;
  if (n == 0) {
    vga_putchar('0');
    return;
  }
  while (n > 0) {
    buf[i++] = digits[n % base];
    n /= base;
  }
  while (i-- > 0)
    vga_putchar(buf[i]);
}

static void print_int(int32_t n) {
  if (n < 0) {
    vga_putchar('-');
    print_uint((uint32_t)(-(n + 1)) + 1, 10);
  } else {
    print_uint((uint32_t)n, 10);
  }
}

/* Minimal va_list without <stdarg.h> — works on i686 cdecl */
typedef char *va_list;
#define va_start(ap, last) ((ap) = (char *)&(last) + sizeof(last))
#define va_arg(ap, type)                                                       \
  (*((type *)(ap)));                                                           \
  (ap) += sizeof(type)
#define va_end(ap) ((ap) = (va_list)0)

void kprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      vga_putchar(*fmt);
      continue;
    }
    fmt++;
    switch (*fmt) {
    case 's': {
      const char *s = va_arg(ap, const char *);
      vga_puts(s ? s : "(null)");
      break;
    }
    case 'd': {
      int32_t n = va_arg(ap, int32_t);
      print_int(n);
      break;
    }
    case 'u': {
      uint32_t n = va_arg(ap, uint32_t);
      print_uint(n, 10);
      break;
    }
    case 'x': {
      uint32_t n = va_arg(ap, uint32_t);
      print_uint(n, 16);
      break;
    }
    case 'X': {
      uint32_t n = va_arg(ap, uint32_t);
      print_uint(n, 16);
      break;
    }
    case 'c': {
      char c = (char)va_arg(ap, int);
      vga_putchar(c);
      break;
    }
    case 'p': {
      uint32_t n = va_arg(ap, uint32_t);
      vga_puts("0x");
      print_uint(n, 16);
      break;
    }
    case '%':
      vga_putchar('%');
      break;
    default:
      vga_putchar('%');
      vga_putchar(*fmt);
      break;
    }
  }
  va_end(ap);
}

/* Simple timer IRQ handler — just increments tick_count for uptime.
 * Does NOT do any scheduling (no context switch). */
static void timer_irq_tick(registers_t *regs) {
  (void)regs;
  timer_tick();
}

/* -------------------------------------------------------------------------
 * kmain — kernel entry point
 * ---------------------------------------------------------------------- */
void kmain(uint32_t magic, multiboot_info_t *mbi) {

  /* === Stage 2: VGA init + banner === */
  vga_init();
  vga_set_color(vga_make_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK));
  kprintf("AnubhavOS booting...\n");
  debug_print("[serial] AnubhavOS booting...\n");
  vga_set_color(vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

  if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
    vga_set_color(vga_make_color(VGA_COLOR_RED, VGA_COLOR_BLACK));
    kprintf("ERROR: Bad Multiboot magic: 0x%x\n", magic);
    khalt();
  }
  kprintf("Multiboot magic OK (0x%x)\n", magic);
  debug_print("[serial] Multiboot OK\n");

  /* === Stage 3: GDT → TSS → IDT === */
  gdt_init();
  debug_print("[serial] GDT loaded\n");

  tss_init((uint32_t)stack_top);
  debug_print("[serial] TSS loaded\n");

  idt_init();
  debug_print("[serial] IDT+IRQ loaded\n");

  kprintf("[GDT/TSS/IDT] Loaded. Interrupts enabled.\n");

  /* === Stage 4: Physical Memory → Paging → Heap === */

  /* 4a: PMM — parse Multiboot memory map */
  pmm_init(mbi);
  debug_print("[serial] PMM ready\n");

  /* 4b: VMM — build kernel page directory, enable paging */
  vmm_init();
  debug_print("[serial] Paging enabled\n");

  /* 4c: Heap — place AFTER both kernel image AND any GRUB modules.
   *
   * BUG FIX: GRUB loads the initramfs module right after the kernel
   * in physical memory. If we blindly start the heap at kernel_end,
   * heap_init() zeroes the module data before we can read it.
   * Solution: scan all module end addresses and start the heap after
   * the highest one. */
  uint32_t safe_end = (uint32_t)kernel_end;
  if (mbi->flags & (1u << 3) && mbi->mods_count > 0) {
    multiboot_mod_t *mods = (multiboot_mod_t *)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count; i++) {
      if (mods[i].mod_end > safe_end)
        safe_end = mods[i].mod_end;
    }
  }
  uint32_t heap_base = (safe_end + 0xFFFu) & ~0xFFFu; /* page-align */
  heap_init(heap_base, 2 * 1024 * 1024);
  debug_print("[serial] Heap ready\n");

  /* === Stage 5: Timer + Scheduler === */
  process_init();

  scheduler_init(); /* Creates idle (PID 0) + sets it as current */

  /* Start the PIT for uptime tracking. Register a simple tick handler
   * that increments the counter WITHOUT doing any scheduling.
   * (The shell runs directly from kmain — no context switching needed.) */
  timer_init(PIT_HZ_DEFAULT);
  irq_register(IRQ_TIMER, timer_irq_tick);

  debug_print("[serial] Scheduler running\n");

  /* === Stage 6: System calls === */
  syscall_init();

  /* === Stage 7: Filesystem === */
  vfs_init();
  ramfs_init();

  /* Stage 7 test: create and read back a file */
  {
    vfs_create("hello.txt");
    int idx = vfs_open("hello.txt");
    if (idx >= 0) {
      const char *msg = "Hello from AnubhavOS!\n";
      uint32_t len = 0;
      const char *p = msg;
      while (*p++)
        len++;
      vfs_write(idx, msg, 0, len);
      vfs_close(idx);

      /* Read back */
      char buf[128];
      idx = vfs_open("hello.txt");
      if (idx >= 0) {
        int32_t n = vfs_read(idx, buf, 0, 127);
        if (n > 0) {
          buf[n] = '\0';
          kprintf("[VFS TEST] Read back: %s", buf);
        }
        vfs_close(idx);
      }
    }
    debug_print("[serial] VFS test OK\n");
  }

  /* === Stage 8: Keyboard + Shell === */
  keyboard_init();
  debug_print("[serial] Keyboard ready\n");

  /* Set up fd_table on idle process so syscalls work from kmain context */
  {
    process_t *idle = scheduler_current();
    if (idle) {
      for (int i = 0; i < MAX_FDS; i++) {
        idle->fd_table[i].file_idx = -1;
        idle->fd_table[i].offset = 0;
        idle->fd_table[i].in_use = 0;
      }
      idle->fd_table[0].in_use = 1;
      idle->fd_table[0].file_idx = -1;
      idle->fd_table[1].in_use = 1;
      idle->fd_table[1].file_idx = -1;
      idle->fd_table[2].in_use = 1;
      idle->fd_table[2].file_idx = -1;
    }
  }

  /* Check if there is a Multiboot module (initramfs) */
  if (mbi->flags & (1u << 3) && mbi->mods_count > 0) {
    multiboot_mod_t *mod = (multiboot_mod_t *)mbi->mods_addr;
    uint32_t mod_start = mod->mod_start;
    uint32_t mod_size = mod->mod_end - mod->mod_start;
    kprintf("[INIT] Loading initramfs (%u bytes)\n", mod_size);
    initramfs_load(mod_start, mod_size);

    /* Load the shell binary into memory at 0x600000 */
    exec("shell.bin");

    /* Run the shell directly from kmain (not via scheduler) */
    kprintf("[INIT] Launching shell...\n");
    void (*shell_entry)(void) = (void (*)(void))0x600000;
    shell_entry();

    /* Shell returned — should not happen */
    kprintf("[KMAIN] Shell exited.\n");
  } else {
    kprintf("[INIT] No initramfs module found.\n");
  }

  kprintf("[KMAIN] Entering idle loop.\n");
  for (;;) {
    __asm__ volatile("sti; hlt");
  }
}
