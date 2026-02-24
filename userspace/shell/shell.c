/*
 * userspace/shell/shell.c — Minimal interactive shell
 *
 * This is a userspace program running in Ring 3.
 * It uses ONLY syscall wrappers — no kernel headers, no libc.
 *
 * Supported commands: help, ls, cat, echo, clear, uptime, meminfo, ps
 */

#include "../lib/string.h"
#include "../lib/syscall_wrappers.h"

#define CMD_BUF_SIZE 256

/* -------------------------------------------------------------------------
 * Helper: print a null-terminated string to stdout
 * ---------------------------------------------------------------------- */
static void print(const char *s) { sys_write(1, s, strlen(s)); }

static void print_num(unsigned int n) {
  char buf[16];
  utoa(n, buf, 10);
  print(buf);
}

static void print_prompt(void) { print("anubhav-os:/ $ "); }

/* -------------------------------------------------------------------------
 * Read a line from stdin (fd 0) with echo and backspace support
 * ---------------------------------------------------------------------- */
static int readline(char *buf, int max) {
  int pos = 0;
  while (pos < max - 1) {
    char c;
    int n = sys_read(0, &c, 1);
    if (n <= 0)
      continue;

    if (c == '\n' || c == '\r') {
      buf[pos] = '\0';
      print("\n");
      return pos;
    }
    if (c == '\b' || c == 127) {
      if (pos > 0) {
        pos--;
        /* Erase character on screen: back, space, back */
        sys_write(1, "\b \b", 3);
      }
      continue;
    }
    buf[pos++] = c;
    sys_write(1, &c, 1); /* Echo */
  }
  buf[pos] = '\0';
  return pos;
}

/* -------------------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------------- */

static void cmd_help(void) {
  print("AnubhavOS Shell Commands:\n");
  print("  help     — show this help\n");
  print("  ls       — list files in ramfs\n");
  print("  cat FILE — display file contents\n");
  print("  echo ... — print arguments\n");
  print("  clear    — clear the screen\n");
  print("  uptime   — seconds since boot\n");
  print("  meminfo  — physical memory usage\n");
  print("  ps       — list running processes\n");
}

/* dirent_t layout — must match kernel VFS */
typedef struct {
  char name[64];
  uint32_t size;
} user_dirent_t;

static void cmd_ls(void) {
  user_dirent_t entries[32];
  int count = sys_readdir(entries, 32);
  if (count < 0) {
    print("ls: readdir failed\n");
    return;
  }
  for (int i = 0; i < count; i++) {
    print("  ");
    print(entries[i].name);
    print("  (");
    print_num(entries[i].size);
    print(" bytes)\n");
  }
  if (count == 0)
    print("  (no files)\n");
}

static void cmd_cat(const char *filename) {
  if (!filename || *filename == '\0') {
    print("cat: usage: cat <filename>\n");
    return;
  }
  int fd = sys_open(filename);
  if (fd < 0) {
    print("cat: file not found: ");
    print(filename);
    print("\n");
    return;
  }
  char buf[512];
  int n;
  while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
    sys_write(1, buf, (size_t)n);
  }
  sys_close(fd);
}

static void cmd_echo(const char *args) {
  if (args && *args) {
    print(args);
  }
  print("\n");
}

static void cmd_clear(void) {
  /* Write 80*25 spaces to clear the VGA screen */
  char line[81];
  memset(line, ' ', 80);
  line[80] = '\0';
  for (int i = 0; i < 25; i++) {
    sys_write(1, line, 80);
  }
  /* Send a special escape: we'll let the kernel handle cursor reset
   * via the clear character. For now just print newlines to scroll. */
  print("\r");
}

static void cmd_uptime(void) {
  uint32_t secs = sys_uptime();
  print("Uptime: ");
  print_num(secs);
  print(" seconds\n");
}

static void cmd_meminfo(void) {
  meminfo_t info;
  if (sys_meminfo(&info) < 0) {
    print("meminfo: failed\n");
    return;
  }
  print("Physical Memory:\n");
  print("  Total: ");
  print_num(info.total_pages * 4);
  print(" KB\n");
  print("  Free:  ");
  print_num(info.free_pages * 4);
  print(" KB\n");
  print("  Used:  ");
  print_num(info.used_pages * 4);
  print(" KB\n");
}

static const char *state_name(uint32_t state) {
  switch (state) {
  case 0:
    return "READY";
  case 1:
    return "RUNNING";
  case 2:
    return "BLOCKED";
  case 3:
    return "ZOMBIE";
  default:
    return "?";
  }
}

static void cmd_ps(void) {
  ps_entry_t entries[32];
  int count = sys_ps(entries, 32);
  if (count < 0) {
    print("ps: failed\n");
    return;
  }
  print("PID  STATE\n");
  for (int i = 0; i < count; i++) {
    print("  ");
    print_num(entries[i].pid);
    print("   ");
    print(state_name(entries[i].state));
    print("\n");
  }
}

/* -------------------------------------------------------------------------
 * _start — entry point (called by proc_iret_trampoline from Ring 0)
 * ---------------------------------------------------------------------- */
void _start(void) {
  print("\n");
  print("  ___                _                ___  ___\n");
  print(" / _ \\              | |               |  \\/  |\n");
  print("/ /_\\ \\_ __  _   _| |__   __ ___   _| .  . |\n");
  print("|  _  | '_ \\| | | | '_ \\ / _` \\ \\ / / |\\/| |\n");
  print("| | | | | | | |_| | |_) | (_| |\\ V /| |  | |\n");
  print("\\_| |_/_| |_|\\__,_|_.__/ \\__,_| \\_/ \\_|  |_/\n");
  print("\n");
  print("Welcome to AnubhavOS! Type 'help' for commands.\n\n");

  char cmd[CMD_BUF_SIZE];

  for (;;) {
    print_prompt();
    int len = readline(cmd, CMD_BUF_SIZE);
    if (len == 0)
      continue;

    /* Parse command */
    if (strcmp(cmd, "help") == 0) {
      cmd_help();
    } else if (strcmp(cmd, "ls") == 0) {
      cmd_ls();
    } else if (strncmp(cmd, "cat ", 4) == 0) {
      cmd_cat(cmd + 4);
    } else if (strncmp(cmd, "echo ", 5) == 0) {
      cmd_echo(cmd + 5);
    } else if (strcmp(cmd, "echo") == 0) {
      cmd_echo("");
    } else if (strcmp(cmd, "clear") == 0) {
      cmd_clear();
    } else if (strcmp(cmd, "uptime") == 0) {
      cmd_uptime();
    } else if (strcmp(cmd, "meminfo") == 0) {
      cmd_meminfo();
    } else if (strcmp(cmd, "ps") == 0) {
      cmd_ps();
    } else if (strcmp(cmd, "exit") == 0) {
      print("Goodbye!\n");
      sys_exit(0);
    } else {
      print("Unknown command: ");
      print(cmd);
      print("\nType 'help' for a list of commands.\n");
    }
  }
}
