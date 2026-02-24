#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include "../../include/types.h"

/*
 * kernel/drivers/timer.h — PIT (Programmable Interval Timer) driver
 *
 * The 8253/8254 PIT has three channels.  Channel 0 fires IRQ0 at a
 * configurable rate.  We use it as the system tick / scheduler preemption
 * source.
 *
 * The PIT input clock is 1193182 Hz.  To get a divisor for frequency f:
 *   divisor = 1193182 / f
 *
 * At 100 Hz: divisor = 11931  → one tick every 10 ms.
 */

#define PIT_HZ_DEFAULT  100     /* Ticks per second */

/* Initialise PIT channel 0 to fire IRQ0 at the given frequency.
 * Does NOT register an IRQ handler — caller must do that. */
void timer_init(uint32_t hz);

/* Advance the tick counter — called by whoever owns IRQ0 */
void timer_tick(void);

/* Return the total number of ticks since timer_init() */
uint32_t timer_get_ticks(void);

/* Return elapsed seconds (integer) since timer_init() */
uint32_t timer_get_seconds(void);

#endif /* DRIVERS_TIMER_H */
