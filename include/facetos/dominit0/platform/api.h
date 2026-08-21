#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef FACET_PLATFORM_SEL4
#include <sel4utils/process.h>
#endif


void platform_init_early(void); // anything the platform needs IMMEDIATELY, usually a NOP

void platform_init(void);       // sets up everything for the platform before other subsystems

void platform_yield(void);	// yields to the microkernel or other tasks

void platform_debug_print(char* str); // prints a debug string

#ifdef FACET_PLATFORM_SEL4
/*
 * Configure and start a process from a complete ELF image mapped into this
 * task. The process object must remain at the same address for the lifetime of
 * the process and may later be passed to sel4utils_destroy_process(). The ELF
 * buffer is no longer needed after this function returns successfully.
 */
int load_and_start_domain(sel4utils_process_t *process,
                          const void *elf_buffer,
                          size_t elf_size,
                          uint8_t priority,
                          int argc,
                          char *argv[]);
#endif
