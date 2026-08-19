#pragma once

#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>

// this is used for inside klog itself, cos we can't easily use the normal klog infrastructure
#define kpanic_halt() do { \
	for(;;) \
	platform_yield(); \
} while(0)

#define kpanic(msg) do {					\
	klog(LOG_ERROR, "KERNEL PANIC: %s\n", (msg));	\
	kpanic_halt(); \
	} while (0)

