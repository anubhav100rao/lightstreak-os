; kernel/proc/context_switch.asm
;
; void context_switch(process_t *current, process_t *next);
;
; Calling convention: cdecl
;   [esp+4]  = current process_t *
;   [esp+8]  = next    process_t *
;
; process_t layout (must match process.h exactly):
;   offset  0: pid              (uint32_t)
;   offset  4: esp              (uint32_t)   ← we read/write here
;   offset  8: kernel_stack_top (uint32_t)
;   offset 12: kernel_stack     (void *)
;   offset 16: page_dir         (page_directory_t *)
;   offset 20: state            (uint32_t)
;   offset 24: exit_code        (int32_t)
;   offset 28: next             (process_t *)
;
; Strategy:
;   1. Push callee-save registers: ebx, esi, edi, ebp
;   2. Save current ESP into current->esp
;   3. Switch CR3 if page directories differ (flush TLB)
;   4. Load next->esp into ESP
;   5. Pop callee-save registers
;   6. 'ret' — lands at the address pushed on next's stack during create

section .text
global context_switch
global proc_iret_trampoline
global kthread_entry

context_switch:
    ; --- Save current process state ---
    push ebp
    push edi
    push esi
    push ebx

    mov eax, [esp + 20]     ; eax = current (adjusted for 4 pushes: 4*4=16 + 4 retaddr = 20)
    mov [eax + 4], esp      ; current->esp = esp

    ; --- Switch page directory if needed ---
    mov ecx, [esp + 24]     ; ecx = next
    mov edx, [ecx + 16]     ; edx = next->page_dir

    mov eax, cr3
    cmp eax, edx
    je .same_dir
    mov cr3, edx            ; load new page directory (flushes TLB)
.same_dir:

    ; --- Load next process state ---
    mov eax, [esp + 24]     ; eax = next (re-read after possible CR3 change)
    mov esp, [eax + 4]      ; esp = next->esp

    ; --- Restore next process's registers ---
    pop ebx
    pop esi
    pop edi
    pop ebp

    ret                     ; jumps to the address at the top of next's stack

; ---------------------------------------------------------------------------
; kthread_entry — 'ret' target for new kernel threads (Ring 0).
;
; When context_switch() executes its final 'ret' for a brand-new kernel
; thread, it lands here.  We arrive straight out of an IRQ handler, so the
; CPU's IF flag is still 0 (interrupt gate cleared it on entry).  We MUST
; call 'sti' before running the thread function, otherwise the thread spins
; forever with interrupts disabled and the tick counter never advances.
;
; Stack on entry (after context_switch's ret popped kthread_entry's address):
;   [esp + 0]  — entry_fn pointer pushed by process_create
;
; Calling convention: cdecl — we pop the fn pointer and call it normally.
; ---------------------------------------------------------------------------
kthread_entry:
    sti                     ; re-enable interrupts (we came from IRQ context)
    pop  eax                ; eax = actual thread entry function
    call eax                ; run the thread (should never return)
    ; Safety net: if the thread function returns, halt.
    cli
.hang:
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
; proc_iret_trampoline — used as the 'ret' target for new Ring-3 processes.
; When context_switch() rets here for the first time, the stack contains an
; iret frame built by process_create_user().  We just execute iret.
; ---------------------------------------------------------------------------
proc_iret_trampoline:
    iret
