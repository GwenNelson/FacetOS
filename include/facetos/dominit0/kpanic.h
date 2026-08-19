#pragma once

#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>

#define kpanic(msg) do {					\
	klog(LOG_ERROR, "KERNEL PANIC: %s\n", (msg));	\
	for (;;)						\
		platform_yield(); \
	} while (0)

