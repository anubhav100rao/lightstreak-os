#ifndef PROC_EXEC_H
#define PROC_EXEC_H

/*
 * kernel/proc/exec.h — User process loader
 *
 * exec() reads a flat binary from ramfs, creates a new address space,
 * copies the binary to 0x400000, sets up a user stack, and adds the
 * process to the scheduler.
 */

#include "../../include/types.h"
#include "process.h"

/* Load a flat binary from ramfs and create a Ring 3 process.
 * Returns the new process, or NULL on failure. */
process_t *exec(const char *filename);

#endif /* PROC_EXEC_H */
