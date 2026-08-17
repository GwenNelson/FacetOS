#pragma once

#include <stdbool.h>
#include <stddef.h>

#define KLOG_MAX_SINKS 8
#define KLOG_EARLY_BUFSIZE (64 * 1024)

enum log_level {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
};

typedef void (*klog_sink_fn)(const char *data, size_t len);

typedef void (*klog_store_fn)(const char *data, size_t len);

struct klog_sink {
    klog_sink_fn write;
    bool enabled;
};

void klog_init_early();    // setup the early boot stuff
void klog_init_postboot(); // setup the "system ready" stuff - after boot, so we don't spam everything all over the console etc

// public locking functions
void klog_lock();
void klog_unlock();

int klog_add_sink(klog_sink_fn write); // tries to add a new sink, returns 0 on success, -1 on failure
void klog(enum log_level level, const char* fmt, ...); // main klog() function, called by other code to log stuff


