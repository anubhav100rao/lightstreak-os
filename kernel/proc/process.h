#ifndef PROC_PROCESS_H
#define PROC_PROCESS_H

/*
 * kernel/proc/process.h — Process Control Block (PCB)
 *
 * Each process has:
 *   - A unique PID
 *   - A saved kernel-mode stack pointer (esp) — used by context_switch
 *   - A kernel stack (allocated from the heap)
 *   - A page directory (for its virtual address space)
 *   - A lifecycle state
 *   - A 'next' pointer for the scheduler's circular run queue
 *
 * For kernel threads, page_dir points to the shared kernel_dir.
 * For user processes (Stage 8), each process gets its own page directory
 * with a private user region and shared kernel mappings.
 */

#include "../../include/types.h"
#include "../mm/vmm.h"

#define KERNEL_STACK_SIZE  8192     /* 8 KB per-process kernel stack */
#define MAX_PROCESSES      64
#define MAX_FDS            32       /* File descriptors per process (Stage 7) */

/* Per-process file descriptor entry */
typedef struct {
    int      file_idx;   /* Index into ramfs file array, -1 = unused */
    uint32_t offset;     /* Current read/write position */
    uint8_t  in_use;     /* 1 = open, 0 = available */
} fd_entry_t;

typedef enum {
    PROC_READY   = 0,
    PROC_RUNNING = 1,
    PROC_BLOCKED = 2,
    PROC_ZOMBIE  = 3,
} proc_state_t;

typedef struct process {
    uint32_t          pid;
    uint32_t          esp;              /* Saved kernel stack pointer */
    uint32_t          kernel_stack_top; /* Top of kernel stack (for TSS.esp0) */
    void             *kernel_stack;     /* kmalloc'd kernel stack base */
    page_directory_t *page_dir;         /* Virtual address space */
    proc_state_t      state;
    int               exit_code;
    struct process   *next;             /* Next in run queue (circular) */
    fd_entry_t        fd_table[MAX_FDS]; /* Per-process file descriptors */
} process_t;

/* Initialise the process subsystem */
void process_init(void);

/*
 * Create a new kernel-mode process.
 * entry_fn: void (*)(void) — the function the process starts executing.
 * Returns the new PCB, or NULL on failure.
 */
process_t *process_create(void (*entry_fn)(void));

/* Create a user-mode process shell (Stage 8 — fills in esp/eip for Ring 3) */
process_t *process_create_user(uint32_t entry_vaddr, uint32_t user_esp,
                                page_directory_t *dir);

/* Free a process's resources (call after it becomes ZOMBIE) */
void process_destroy(process_t *p);

/* Return current PID counter (for getpid syscall) */
uint32_t process_next_pid(void);

#endif /* PROC_PROCESS_H */
