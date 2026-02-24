# AnubhavOS — Testing Guide

Step-by-step manual testing plan for all 8 stages. Run each section in order.

> **Prerequisites**: Cross-compiler (`i686-elf-gcc`), NASM, QEMU, and `i686-elf-grub-mkrescue` on `$PATH`.

---

## 🔨 Step 1: Build

```bash
cd ~/development/anubhav-os
make clean && make
```

**Pass if**: No compilation errors. Last line says `[ISO] Created build/anubhav-os.iso`.

You should see all these steps succeed:
- 18 kernel `.c` files compiled
- 4 `.asm` files assembled
- `[LD] Linked build/kernel.elf`
- `[HOSTCC] Built build/mkramfs`
- 2 userspace `.c` files compiled
- `[OBJCOPY] Created build/shell.bin`
- `[INITRAMFS] Created build/initramfs.img`
- `[ISO] Created build/anubhav-os.iso`

---

## 🚀 Step 2: Boot

```bash
make run
```

This opens a **QEMU window** (the OS screen) and prints **serial debug output** in your terminal.

Watch BOTH displays simultaneously:
- **QEMU window** = what a user would see on a real monitor
- **Terminal** = debug/serial log for developers

---

## ✅ Step 3: Verify Boot Messages

Check the QEMU screen shows these messages **in order**:

| # | Message on QEMU screen | Stage |
|---|----------------------|-------|
| 1 | `AnubhavOS booting...` (green text) | Stage 2 |
| 2 | `Multiboot magic OK (0x2badb002)` | Stage 2 |
| 3 | `[GDT/TSS/IDT] Loaded. Interrupts enabled.` | Stage 3 |
| 4 | `[PMM] Total: XX MB  Free: XX MB  Used by kernel: XXXX KB` | Stage 4 |
| 5 | `[VMM] Paging enabled. Identity-mapped first 8 MB.` | Stage 4 |
| 6 | `[HEAP] Initialised at 0xXXXXXX, size 2048 KB` | Stage 4 |
| 7 | `[PROC] Process subsystem initialised` | Stage 5 |
| 8 | `[SCHED] Scheduler initialised (idle PID 0)` | Stage 5 |
| 9 | `[PIT] Timer initialised at 100 Hz ...` | Stage 5 |
| 10 | `[SYSCALL] System call interface ready (int 0x80, 10 syscalls)` | Stage 6 |
| 11 | `[VFS] Virtual filesystem initialised` | Stage 7 |
| 12 | `[RAMFS] In-memory filesystem mounted` | Stage 7 |
| 13 | `[VFS TEST] Read back: Hello from AnubhavOS!` | Stage 7 |
| 14 | `[KBD] PS/2 keyboard driver initialised` | Stage 8 |
| 15 | `[INIT] Loading initramfs (XXXX bytes)` | Stage 8 |
| 16 | `[RAMFS] Loading initramfs: 2 files` | Stage 8 |
| 17 | `[EXEC] Loaded 'shell.bin' ... → PID X at 0x400000` | Stage 8 |
| 18 | `[KMAIN] Entering idle loop.` | Stage 8 |

And in your **terminal** (serial output):
```
[serial] AnubhavOS booting...
[serial] Multiboot OK
[serial] GDT loaded
[serial] TSS loaded
[serial] IDT+IRQ loaded
[serial] PMM ready
[serial] Paging enabled
[serial] Heap ready
[serial] Scheduler running
[serial] Syscall init
[serial] VFS test OK
[serial] Keyboard ready
```

> [!IMPORTANT]
> If you see `[RAMFS] WARNING: bad initramfs magic 0x0`, the heap is overwriting the GRUB module. This was a known bug (now fixed) — make sure you have the latest `kernel.c` that scans `mbi->mods_addr` to place the heap after all modules.

---

## 🐚 Step 4: Test Shell Commands

Once you see the shell banner and `anubhav-os:/ $` prompt, test these commands:

### 4.1 — `help`
Type `help` and press Enter.

**Expected**: Lists all commands (help, ls, cat, echo, clear, uptime, meminfo, ps).

### 4.2 — `ls`
Type `ls` and press Enter.

**Expected**:
```
  shell.bin  (XXXX bytes)
  hello.txt  (111 bytes)
```

### 4.3 — `cat hello.txt`
Type `cat hello.txt` and press Enter.

**Expected**: Prints the contents of hello.txt:
```
Welcome to AnubhavOS!
This is a hobby operating system built from scratch.
Type 'help' for a list of commands.
```

### 4.4 — `echo Hello World`
Type `echo Hello World` and press Enter.

**Expected**: Prints `Hello World`.

### 4.5 — `uptime`
Type `uptime` and press Enter. Wait a few seconds, type it again.

**Expected**: `Uptime: X seconds` — the number should increase between calls.

### 4.6 — `meminfo`
Type `meminfo` and press Enter.

**Expected**: Shows Total, Free, and Used memory in KB.

### 4.7 — `ps`
Type `ps` and press Enter.

**Expected**: Shows a PID/STATE table with at least idle (PID 0) and the shell process.

### 4.8 — `clear`
Type `clear` and press Enter.

**Expected**: Screen fills with spaces (visual clear effect).

### 4.9 — Unknown command
Type `foobar` and press Enter.

**Expected**: `Unknown command: foobar` + help hint.

---

## 🔍 Step 5: Edge Cases

| # | Test | How to do it | Expected |
|---|------|-------------|----------|
| 1 | Empty enter | Press Enter with no input | New prompt, no crash |
| 2 | Backspace | Type `helo`, press Backspace, type `lo` → `help` | Character erased on screen |
| 3 | Shift keys | Type `Hello WORLD !@#` | Correct uppercase + symbols |
| 4 | cat missing file | Type `cat nonexist.txt` | `file not found: nonexist.txt` |
| 5 | cat no argument | Type `cat` alone | Usage message |
| 6 | Repeated uptime | Run `uptime` twice, 5s apart | Number increases |

---

## 🛑 Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| QEMU closes immediately | Triple fault in boot | Run `make debug`, attach GDB |
| `KERNEL EXCEPTION ISR 14` | Page fault | Note the `EIP` and `CR2` in dump |
| `KERNEL EXCEPTION ISR 13` | Bad segment selector (GPF) | Check GDT Ring 3 values |
| `bad initramfs magic 0x0` | Heap overwrites module data | Update `kernel.c` heap placement (see fix above) |
| No shell prompt | exec() failed | Check serial for `[EXEC]` messages |
| Keyboard doesn't respond | IRQ1 not registered | Check `keyboard_init()` is called |
| `ls` shows no files | Initramfs not loaded | Check `grub.cfg` has `module /boot/initramfs.img` |

---

## 🔧 GDB Debug Mode (Optional)

```bash
# Terminal 1: start QEMU frozen
make debug

# Terminal 2: attach GDB
gdb build/kernel.elf -ex 'target remote localhost:1234'
```

Useful breakpoints:
```gdb
break kmain               # Kernel entry
break syscall_handler      # Any syscall
break exec                 # Shell loading
break keyboard_getchar     # Keyboard input
continue                   # Resume
info registers             # Dump registers
```

---

*Last updated: 2026-02-24*
