#pragma once

#include <stddef.h>

#include <facetos/interfaces/ILoggingConfig.h>
#include <facetos/interfaces/ILoggingSink.h>

#define KLOG_MAX_SINKS 8
// we only want a small buffer, 2KB is plenty before the dynamic heap is online and we can use that
#define KLOG_EARLY_BUFSIZE (2 * 1024)

enum log_level {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
};

typedef struct KlogSinkBinding {
    FacetString name;
    ILoggingSink *sink;
} KlogSinkBinding;

/* The emergency sink must be static, allocation-free, and directly callable. */
void klog_init_early(ILoggingSink *emergency_sink);

/* Consumes immutable domain policy, copies the early retained bytes into the
 * dynamic buffer, and atomically switches normal routing to configured sinks. */
int klog_init_postboot(ILoggingConfig *config,
                       const KlogSinkBinding *bindings,
                       size_t binding_count);

// public locking functions
void klog_lock(void);
void klog_unlock(void);

void klog(enum log_level level, const char* fmt, ...); // main klog() function, called by other code to log stuff
void klog_dump_debug(void); // dump debug data
