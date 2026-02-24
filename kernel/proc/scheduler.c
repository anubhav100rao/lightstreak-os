/*
 * kernel/proc/scheduler.c — Round-robin preemptive scheduler
 *
 * Run queue: a circular singly-linked list of READY processes.
 * The 'current' pointer always points to the process that owns the CPU.
 *
 * On each timer tick:
 *   1. Walk to the next READY process in the ring.
 *   2. Update TSS.esp0 so the CPU uses the right kernel stack if an
 *      interrupt arrives while the new process is in Ring 3.
 *   3. Call context_switch(current, next).
 *
 * The idle process runs when nothing else is READY.  It simply halts
 * (hlt) with interrupts enabled so the next timer tick wakes it up.
 */

#include "scheduler.h"
#include "process.h"
#include "../arch/tss.h"
#include "../drivers/timer.h"
#include "../kernel.h"

/* Assembly: void context_switch(process_t *cur, process_t *nxt) */
extern void context_switch(process_t *cur, process_t *nxt);

static process_t *current   = NULL;   /* Currently running process */
static process_t *run_queue = NULL;   /* Head of the circular run queue */

/* -------------------------------------------------------------------------
 * Idle process — runs when nobody else is READY
 * ---------------------------------------------------------------------- */
static void idle_fn(void) {
    for (;;) {
        __asm__ volatile ("sti; hlt"); /* Wait for interrupt */
    }
}

/* -------------------------------------------------------------------------
 * Queue management
 * ---------------------------------------------------------------------- */

void scheduler_add(process_t *p) {
    if (!p) return;
    p->state = PROC_READY;

    if (!run_queue) {
        /* First process — make a self-loop */
        run_queue = p;
        p->next   = p;
    } else {
        /* Insert after run_queue (i.e. just before its ->next) */
        p->next          = run_queue->next;
        run_queue->next  = p;
    }
}

void scheduler_remove(process_t *p) {
    if (!p || !run_queue) return;

    /* Find the predecessor in the circular list */
    process_t *prev = run_queue;
    while (prev->next != p && prev->next != run_queue) {
        prev = prev->next;
    }
    if (prev->next != p) return; /* Not found */

    if (p->next == p) {
        /* Only one element */
        run_queue = NULL;
    } else {
        prev->next = p->next;
        if (run_queue == p) run_queue = p->next;
    }
    p->next = NULL;
}

process_t *scheduler_current(void) { return current; }

/* -------------------------------------------------------------------------
 * scheduler_init — create idle process and set it as current
 * ---------------------------------------------------------------------- */
void scheduler_init(void) {
    process_t *idle = process_create(idle_fn);
    if (!idle) {
        kprintf("[SCHED] FATAL: failed to create idle process\n");
        khalt();
    }
    idle->pid = 0; /* PID 0 = idle */
    scheduler_add(idle);
    current = idle;
    current->state = PROC_RUNNING;
    kprintf("[SCHED] Scheduler initialised (idle PID 0)\n");
}

/* -------------------------------------------------------------------------
 * scheduler_tick — called from timer IRQ handler
 * ---------------------------------------------------------------------- */
void scheduler_tick(registers_t *regs) {
    (void)regs;
    timer_tick();               /* Advance global tick counter */

    if (!run_queue || !current) return;

    /* Find the next READY process (skip BLOCKED/ZOMBIE) */
    process_t *next = current->next;
    int steps = 0;
    int total = 64; /* Bound the search to avoid infinite loops */

    while (steps < total && next->state != PROC_READY && next != current) {
        next = next->next;
        steps++;
    }

    if (next == current) return; /* Nothing else to run */
    if (next->state != PROC_READY) return;

    /* Update running states */
    if (current->state == PROC_RUNNING) current->state = PROC_READY;
    next->state = PROC_RUNNING;

    process_t *prev = current;
    current = next;

    /* CRITICAL: update TSS.esp0 so Ring-3 interrupts use the right stack */
    tss_set_kernel_stack(current->kernel_stack_top);

    /* Perform the actual context switch */
    context_switch(prev, current);
}
