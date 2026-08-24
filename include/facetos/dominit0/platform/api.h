#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct ILoggingSink ILoggingSink;

#ifdef FACET_PLATFORM_SEL4
#include <sel4utils/process.h>
#endif


void platform_init_early(void); // anything the platform needs IMMEDIATELY, usually a NOP

void platform_init(void);       // sets up everything for the platform before other subsystems

/* The emergency sink is available before platform_init() and must not
 * allocate or use RPC. Configured sink lookup is available after it. */
ILoggingSink *platform_get_early_logging_sink(void);
int platform_get_logging_sink(const char *type, ILoggingSink **result);

typedef struct PlatformConfigSource {
    const uint8_t *data;
    size_t size;
} PlatformConfigSource;

typedef enum PlatformConfigSourceStatus {
    PLATFORM_CONFIG_SOURCE_FOUND = 0,
    PLATFORM_CONFIG_SOURCE_ABSENT = 1,
    PLATFORM_CONFIG_SOURCE_INVALID = -1,
    PLATFORM_CONFIG_SOURCE_DUPLICATE = -2,
} PlatformConfigSourceStatus;

PlatformConfigSourceStatus platform_get_config_source(
    PlatformConfigSource *source);

void platform_start_initial_domain(void);

void platform_yield(void);	// yields to the microkernel or other tasks

void platform_debug_print(char* str); // prints a debug string

#ifdef FACET_PLATFORM_SEL4
/*
 * Configure and start a process from a complete ELF image mapped into this
 * task. The process object must remain at the same address for the lifetime of
 * the process and may later be passed to sel4utils_destroy_process(). The ELF
 * buffer is no longer needed after this function returns successfully. argv
 * must contain at least argv[0]; argv[1] is inserted by this function and
 * contains the child-visible debug endpoint CPtr. Existing arguments from
 * argv[1] onward are shifted up by one slot.
 */
int load_and_start_domain(sel4utils_process_t *process,
                          const void *elf_buffer,
                          size_t elf_size,
                          uint8_t priority,
                          int argc,
                          char *argv[]);
#endif
