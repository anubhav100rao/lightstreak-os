# AnubhavOS — Device Drivers

AnubhavOS includes three hardware drivers: VGA text-mode display, PIT timer,
and PS/2 keyboard. All drivers communicate with hardware via x86 port I/O
(`inb`/`outb`) or memory-mapped I/O.

---

## Table of Contents

1. [Port I/O Primitives](#1-port-io-primitives)
2. [VGA Text-Mode Driver](#2-vga-text-mode-driver)
3. [PIT Timer Driver](#3-pit-timer-driver)
4. [PS/2 Keyboard Driver](#4-ps2-keyboard-driver)
5. [PIC (8259A) — Interrupt Controller](#5-pic-8259a--interrupt-controller)

---

## 1. Port I/O Primitives

**Source**: `kernel/arch/io.h`

x86 CPUs have a separate 16-bit I/O address space (ports 0x0000–0xFFFF)
accessible via `IN` and `OUT` instructions. AnubhavOS wraps these in inline C
functions:

```c
// Read a byte from an I/O port
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

// Write a byte to an I/O port
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

// Wait for an I/O operation to complete (~1μs delay)
static inline void io_wait(void) {
    outb(0x80, 0);   // port 0x80 is unused, write causes a small delay
}
```

These are used extensively by all three drivers and the PIC.

---

## 2. VGA Text-Mode Driver

**Source**: `kernel/drivers/vga.c`, `kernel/drivers/vga.h`

### 2.1 How VGA Text Mode Works

The VGA text buffer is a **memory-mapped** region at physical address `0xB8000`.
It holds 80 columns × 25 rows = 2000 character cells, each stored as 2 bytes:

```
Byte 0: ASCII character (e.g., 'H' = 0x48)
Byte 1: Attribute byte
         ┌─────────┬────────────┐
         │ Bits 7-4 │ Bits 3-0   │
         │ BG color │ FG color   │
         └─────────┴────────────┘
```

**Color values** (4-bit):

```c
enum vga_color {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW        = 14,
    VGA_COLOR_WHITE         = 15,
};
```

### 2.2 Key Functions

```c
// Initialise: clear screen, set cursor to (0,0)
void vga_init(void);

// Write a single character at current cursor position
void vga_putchar(char c);

// Set foreground/background color
void vga_set_color(uint8_t color);
uint8_t vga_make_color(enum vga_color fg, enum vga_color bg);

// Move hardware cursor to match software position
void vga_move_cursor(void);
```

### 2.3 Special Character Handling

`vga_putchar` handles these control characters:

| Character | ASCII | Behaviour |
|-----------|-------|-----------|
| `\n`      | 0x0A  | Move to start of next line |
| `\r`      | 0x0D  | Move to start of current line |
| `\t`      | 0x09  | Advance to next 8-column tab stop |
| `\b`      | 0x08  | Move cursor back one column (no erase) |

### 2.4 Scrolling

When the cursor moves past row 24 (the last visible row), the driver scrolls:

```c
static void vga_scroll(void) {
    // Move rows 1-24 up to 0-23
    for (int i = 0; i < 24 * 80; i++) {
        vga_buffer[i] = vga_buffer[i + 80];
    }
    // Clear the last row
    for (int i = 24 * 80; i < 25 * 80; i++) {
        vga_buffer[i] = (uint16_t)' ' | (uint16_t)(color << 8);
    }
}
```

### 2.5 Hardware Cursor

The VGA controller has a hardware cursor (blinking underscore). Its position is
set via two I/O port writes:

```c
void vga_move_cursor(void) {
    uint16_t pos = vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 14);           // CRT register: cursor high byte
    outb(0x3D5, pos >> 8);
    outb(0x3D4, 15);           // CRT register: cursor low byte
    outb(0x3D5, pos & 0xFF);
}
```

### 2.6 Why 0xB8000?

This address is **hardwired into x86 PC hardware** since the IBM PC (1981).
The 640KB–1MB region is a "memory hole" reserved for video, BIOS ROM, and
hardware:

```
0xA0000 – VGA graphics framebuffer  (64 KB)
0xB0000 – MDA monochrome text       (32 KB)
0xB8000 – CGA/VGA color text        (32 KB) ← we use this
0xC0000 – Video BIOS ROM            (32 KB)
0xF0000 – System BIOS ROM           (64 KB)
```

---

## 3. PIT Timer Driver

**Source**: `kernel/drivers/timer.c`, `kernel/drivers/timer.h`

### 3.1 How the PIT Works

The Intel 8253/8254 Programmable Interval Timer generates periodic IRQ0
interrupts at a programmable frequency. It has an internal oscillator running at
**1,193,182 Hz**. We program a divisor to set our desired frequency:

```
divisor = 1,193,182 / desired_hz
```

### 3.2 Initialisation

```c
#define PIT_CMD   0x43    // Command port
#define PIT_CH0   0x40    // Channel 0 data port
#define PIT_BASE  1193182 // Base oscillator frequency

void timer_init(uint32_t hz) {
    uint32_t divisor = PIT_BASE / hz;  // e.g., 1193182/100 = 11932

    outb(PIT_CMD, 0x36);               // Channel 0, lo/hi byte, rate generator
    outb(PIT_CH0, divisor & 0xFF);      // Low byte
    outb(PIT_CH0, (divisor >> 8) & 0xFF); // High byte

    timer_frequency = hz;
    kprintf("[PIT] Timer initialised at %u Hz (divisor=%u)\n", hz, divisor);
}
```

### 3.3 Tick Handler

Every time the PIT fires IRQ0, the kernel's timer handler increments a counter:

```c
static volatile uint32_t tick_count = 0;

void timer_tick(void) {
    tick_count++;
}

uint32_t timer_get_seconds(void) {
    return tick_count / timer_frequency;  // e.g., 500 ticks / 100 Hz = 5 seconds
}
```

At 100 Hz, the timer fires every 10 milliseconds.

### 3.4 IRQ Registration

The timer is connected to **IRQ0**, which is remapped by the PIC to **IDT vector 32**:

```c
// In kernel.c:
timer_init(100);                          // 100 Hz = 10ms intervals
irq_register(IRQ_TIMER, timer_irq_tick);  // IRQ0 → timer_irq_tick()
```

---

## 4. PS/2 Keyboard Driver

**Source**: `kernel/drivers/keyboard.c`, `kernel/drivers/keyboard.h`

### 4.1 How PS/2 Keyboards Work

The PS/2 keyboard controller (Intel 8042) uses two I/O ports:
- **Port 0x60**: Data register (read scancodes)
- **Port 0x64**: Status register

When a key is pressed or released, the keyboard controller generates **IRQ1**
(remapped to IDT vector 33). The interrupt handler reads the scancode from port
0x60.

### 4.2 Scan Code Set 1

AnubhavOS uses **Scan Code Set 1** (the default for PS/2 keyboards):

- **Key press**: Sends a 1-byte scancode (0x00–0x7F)
- **Key release**: Sends the scancode with bit 7 set (0x80–0xFF)

Example scancodes:

| Key | Press | Release | ASCII |
|-----|-------|---------|-------|
| A   | 0x1E  | 0x9E    | 'a'   |
| B   | 0x30  | 0xB0    | 'b'   |
| 1   | 0x02  | 0x82    | '1'   |
| Enter | 0x1C | 0x9C   | '\n'  |
| Space | 0x39 | 0xB9   | ' '   |
| L-Shift | 0x2A | 0xAA | (modifier) |

### 4.3 IRQ Handler

```c
static void keyboard_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t scancode = inb(KBD_DATA_PORT);   // read from port 0x60

    // Handle shift key press/release
    if (scancode == 0x2A || scancode == 0x36) {  // L-Shift or R-Shift press
        shift_held = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {  // L-Shift or R-Shift release
        shift_held = 0;
        return;
    }

    // Ignore all key releases (bit 7 set)
    if (scancode & 0x80) return;

    // Convert scancode to ASCII using lookup table
    char c;
    if (shift_held)
        c = scancode_to_ascii_shift[scancode];   // 'A', '!', etc.
    else
        c = scancode_to_ascii[scancode];         // 'a', '1', etc.

    if (c == 0) return;   // unmapped key (F1, Ctrl, etc.)

    // Push into ring buffer
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}
```

### 4.4 Ring Buffer

Characters are stored in a 256-byte circular buffer:

```
             ┌───────────────────────┐
             │  h  e  l  l  o  _  _  │
             └───────────────────────┘
                ↑                 ↑
              tail              head
              (read)           (write)

When tail == head → buffer is empty (keyboard_getchar blocks)
When (head+1) % 256 == tail → buffer is full (new keys dropped)
```

### 4.5 Blocking Read

```c
char keyboard_getchar(void) {
    // Busy-wait with interrupts enabled until a character arrives
    while (kbd_tail == kbd_head) {
        __asm__ volatile("sti; hlt");   // sleep until next interrupt
    }
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}
```

The `sti; hlt` instruction pair is crucial:
1. `sti` enables interrupts (they were disabled by the interrupt gate)
2. `hlt` halts the CPU until the next interrupt fires
3. When keyboard IRQ1 fires, the CPU wakes, the handler adds a character to
   the buffer, and we check again

### 4.6 Supported Keys

| Category | Keys |
|----------|------|
| Letters  | a-z, A-Z (with Shift) |
| Numbers  | 0-9, shifted symbols (!@#$%^&*()) |
| Punctuation | `,./;'[]\ ` and shifted equivalents |
| Control | Enter, Backspace, Tab, Space, Escape |
| Modifiers | Left Shift, Right Shift |

**Not supported**: Ctrl, Alt, Caps Lock, function keys (F1-F12), arrow keys,
numpad, multimedia keys.

---

## 5. PIC (8259A) — Interrupt Controller

**Source**: `kernel/arch/irq.c`, `kernel/arch/irq.h`

### 5.1 What the PIC Does

The 8259A Programmable Interrupt Controller routes hardware interrupt signals
(IRQs) to CPU interrupt vectors. Two cascaded PICs handle 16 IRQ lines:

```
              ┌──────────┐
   IRQ0  ────►│          │
   IRQ1  ────►│ Master   │──── INT pin ──► CPU
   IRQ2  ────►│ PIC      │
   ...   ────►│ (0x20)   │
   IRQ7  ────►│          │
              └──────────┘
                   ↑ IRQ2 = cascade
              ┌──────────┐
   IRQ8  ────►│          │
   IRQ9  ────►│ Slave    │
   ...   ────►│ PIC      │
   IRQ15 ────►│ (0xA0)   │
              └──────────┘
```

### 5.2 PIC Remapping

By default, the BIOS maps IRQ0-7 to IDT vectors 8-15 — **conflicting with CPU
exceptions** (8 = Double Fault, 13 = GPF, 14 = Page Fault). We must remap them:

```c
void pic_remap(void) {
    // ICW1: start initialisation in cascade mode
    outb(0x20, 0x11);   // Master PIC command
    outb(0xA0, 0x11);   // Slave PIC command
    io_wait();

    // ICW2: new vector offsets
    outb(0x21, 0x20);   // Master: IRQ 0-7  → vectors 32-39
    outb(0xA1, 0x28);   // Slave:  IRQ 8-15 → vectors 40-47
    io_wait();

    // ICW3: cascade wiring
    outb(0x21, 0x04);   // Master: slave on IRQ2
    outb(0xA1, 0x02);   // Slave: cascade identity
    io_wait();

    // ICW4: 8086 mode
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();

    // Unmask ALL IRQs
    outb(0x21, 0x00);   // Master: all unmasked
    outb(0xA1, 0x00);   // Slave: all unmasked
}
```

**After remapping**:

| IRQ | Device    | IDT Vector |
|-----|-----------|-----------|
| 0   | PIT Timer | 32        |
| 1   | Keyboard  | 33        |
| 2   | Cascade   | 34        |
| ...   | ...     | ...       |
| 14  | ATA Disk  | 46        |

### 5.3 End of Interrupt (EOI)

After handling an IRQ, we must send an EOI command to the PIC to re-enable
that IRQ line:

```c
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(0xA0, 0x20);  // EOI to slave PIC
    outb(0x20, 0x20);      // EOI to master PIC (always needed)
}
```

> **Critical**: In AnubhavOS, EOI is sent **before** calling the handler
> function. This is because `scheduler_tick` may context-switch and never
> return to send EOI, which would permanently block all future IRQs.
