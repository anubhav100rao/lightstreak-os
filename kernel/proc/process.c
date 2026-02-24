/*
 * kernel/proc/process.c — Process creation and lifecycle
 *
 * process_create() sets up a PCB and fakes an initial stack frame so that
 * when context_switch() performs its first 'ret' into this process, the CPU
 * lands at entry_fn() as if it had been called normally.
 *
 * Initial kernel-stack layout (grows downward, filled by process_create):
 *
 *   [esp+ 0]  ebx  = 0  \
 *   [esp+ 4]  esi  = 0   | callee-saves popped by context_switch
 *   [esp+ 8]  edi  = 0   |
 *   [esp+12]  ebp  = 0  /
 *   [esp+16]  kthread_entry     ← context_switch 'ret's here
 *   [esp+20]  entry_fn          ← kthread_entry pops this, calls sti, then calls it
 *
 * context_switch() in assembly does:
 *   push ebp, push edi, push esi, push ebx   (callee-saves)
 *   mov [cur->esp], esp
 *   mov esp, [next->esp]
 *   pop ebx, pop esi, pop edi, pop ebp
 *   ret         ← first time: lands in kthread_entry (sti + call entry_fn)
 *               ← subsequent times: returns through scheduler_tick → iret
 */

#include "process.h"
#include "../mm/heap.h"
#include "../kernel.h"
#include "../mm/vmm.h"

static uint32_t next_pid = 1;

uint32_t process_next_pid(void) { return next_pid; }

void process_init(void) {
    next_pid = 1;
    kprintf("[PROC] Process subsystem initialised\n");
}

process_t *process_create(void (*entry_fn)(void)) {
    process_t *p = (process_t *)kzalloc(sizeof(process_t));
    if (!p) return NULL;

    /* Allocate kernel stack */
    p->kernel_stack = kzalloc(KERNEL_STACK_SIZE);
    if (!p->kernel_stack) { kfree(p); return NULL; }

    p->pid              = next_pid++;
    p->page_dir         = vmm_get_kernel_dir();
    p->state            = PROC_READY;
    p->kernel_stack_top = (uint32_t)p->kernel_stack + KERNEL_STACK_SIZE;

    /*
     * Set up the initial stack frame for a kernel thread.
     *
     * Kernel threads start life from inside an IRQ handler, so the CPU's
     * IF flag is 0 when context_switch() first rets into the thread.
     * We route through kthread_entry (in context_switch.asm) which calls
     * 'sti' before invoking the real entry function.
     *
     * Stack layout (top = lowest address):
     *   [esp+ 0]  ebx  = 0  \
     *   [esp+ 4]  esi  = 0   | callee-saves popped by context_switch
     *   [esp+ 8]  edi  = 0   |
     *   [esp+12]  ebp  = 0  /
     *   [esp+16]  kthread_entry   ← context_switch 'ret's here
     *   [esp+20]  entry_fn        ← kthread_entry pops this into eax
     */
    extern void kthread_entry(void);
    uint32_t *sp = (uint32_t *)p->kernel_stack_top;

    *--sp = (uint32_t)entry_fn;    /* kthread_entry pops and calls this */
    *--sp = (uint32_t)kthread_entry; /* context_switch rets here */

    /* Callee-saved registers (ebx, esi, edi, ebp) — all 0 initially */
    *--sp = 0; /* ebp */
    *--sp = 0; /* edi */
    *--sp = 0; /* esi */
    *--sp = 0; /* ebx */

    p->esp = (uint32_t)sp;   /* context_switch loads this into ESP */
    return p;
}

process_t *process_create_user(uint32_t entry_vaddr, uint32_t user_esp,
                                page_directory_t *dir) {
    process_t *p = (process_t *)kzalloc(sizeof(process_t));
    if (!p) return NULL;

    p->kernel_stack = kzalloc(KERNEL_STACK_SIZE);
    if (!p->kernel_stack) { kfree(p); return NULL; }

    p->pid              = next_pid++;
    p->page_dir         = dir;
    p->state            = PROC_READY;
    p->kernel_stack_top = (uint32_t)p->kernel_stack + KERNEL_STACK_SIZE;

    /*
     * For a Ring-3 process, we need an iret frame on the kernel stack.
     * When context_switch() rets into this frame, an iret pops:
     *   eip, cs, eflags, esp, ss
     * in that order — switching to Ring 3.
     *
     * Stack layout (from top, pushed last → first):
     *   ss         (user data selector = GDT_USER_DATA | RPL3 = 0x23)
     *   user esp
     *   eflags     (IF=1 so interrupts enabled in user mode)
     *   cs         (user code selector = GDT_USER_CODE | RPL3 = 0x1B)
     *   eip        (entry_vaddr)
     *   — iret boundary —
     *   callee-save regs (for context_switch)
     *   return addr → iret_trampoline
     */
    uint32_t *sp = (uint32_t *)p->kernel_stack_top;

    /* iret frame */
    *--sp = 0x23;            /* ss  — user data */
    *--sp = user_esp;        /* esp — user stack */
    *--sp = 0x202;           /* eflags: IF=1, reserved bit 1=1 */
    *--sp = 0x1B;            /* cs  — user code */
    *--sp = entry_vaddr;     /* eip */

    /* context_switch callee-saves (point 'ret' to iret trampoline) */
    extern void proc_iret_trampoline(void);
    *--sp = (uint32_t)proc_iret_trampoline; /* ret addr for context_switch */
    *--sp = 0; /* ebp */
    *--sp = 0; /* edi */
    *--sp = 0; /* esi */
    *--sp = 0; /* ebx */

    p->esp = (uint32_t)sp;
    return p;
}

void process_destroy(process_t *p) {
    if (!p) return;
    if (p->kernel_stack) kfree(p->kernel_stack);
    kfree(p);
}
