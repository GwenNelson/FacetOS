#pragma once

#include <stddef.h>
#include <stdint.h>

#include <facetos/dominit0/config.h>

typedef struct ILoggingSink ILoggingSink;


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
PlatformConfigSourceStatus platform_get_boot_module(
    const char *name, PlatformConfigSource *source);

/* Starts one configured domain and returns platform-owned runtime state.
 * A NULL result means the domain could not be started. */
void *platform_start_domain(CurrentDomain *current);

void platform_yield(void);	// yields to the microkernel or other tasks

void platform_debug_print(char* str); // prints a debug string
