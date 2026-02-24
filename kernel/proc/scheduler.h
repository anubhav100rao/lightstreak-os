#ifndef PROC_SCHEDULER_H
#define PROC_SCHEDULER_H

/*
 * kernel/proc/scheduler.h — Round-robin preemptive scheduler
 *
 * The scheduler maintains a circular singly-linked run queue of READY
 * processes.  On each timer tick (IRQ0), scheduler_tick() picks the next
 * process and calls context_switch() to hand the CPU to it.
 *
 * TSS.esp0 is updated before every context switch so the CPU knows which
 * kernel stack to use if the new process (running in Ring 3) takes a trap.
 */

#include "../../include/types.h"
#include "../arch/idt.h"   /* registers_t */
#include "process.h"

/* Initialise the scheduler (creates the idle process PCB) */
void scheduler_init(void);

/* Add a process to the run queue */
void scheduler_add(process_t *p);

/* Remove a process from the run queue (called on ZOMBIE/exit) */
void scheduler_remove(process_t *p);

/* Called by the timer IRQ handler — selects next process and switches */
void scheduler_tick(registers_t *regs);

/* Return the currently running process */
process_t *scheduler_current(void);

#endif /* PROC_SCHEDULER_H */
