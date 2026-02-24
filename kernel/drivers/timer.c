/*
 * kernel/drivers/timer.c — PIT channel 0 timer driver
 *
 * Programmes the 8253/8254 PIT to generate IRQ0 at the requested frequency.
 * The IRQ0 handler increments a global tick counter each time it fires.
 * The scheduler hooks into this via irq_register(0, scheduler_tick).
 */

#include "timer.h"
#include "../arch/irq.h"
#include "../arch/io.h"
#include "../kernel.h"

/* PIT I/O ports */
#define PIT_CHANNEL0_DATA  0x40
#define PIT_CMD_PORT       0x43

/*
 * Command byte for channel 0:
 *   bits [7:6] = 00  → channel 0
 *   bits [5:4] = 11  → lo/hi byte access
 *   bits [3:1] = 011 → mode 3 (square wave)
 *   bit  [0]   = 0   → binary (not BCD)
 */
#define PIT_CMD_CHANNEL0_RATE  0x36

#define PIT_BASE_FREQ   1193182u   /* Hz — PIT crystal frequency */

volatile uint32_t tick_count = 0;   /* extern-accessible for scheduler */
static uint32_t ticks_per_second = PIT_HZ_DEFAULT;

/* Called by whoever owns IRQ0 (the scheduler) to advance the counter */
void timer_tick(void) {
    tick_count++;
}

void timer_init(uint32_t hz) {
    ticks_per_second = hz;
    uint32_t divisor = PIT_BASE_FREQ / hz;

    /* Programme PIT channel 0 to rate-generator mode at 'hz' */
    outb(PIT_CMD_PORT, PIT_CMD_CHANNEL0_RATE);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    /* NOTE: IRQ0 handler is registered externally (by scheduler or caller) */
    kprintf("[PIT] Timer initialised at %u Hz (divisor %u)\n", hz, divisor);
}

uint32_t timer_get_ticks(void)   { return tick_count; }
uint32_t timer_get_seconds(void) { return tick_count / ticks_per_second; }
