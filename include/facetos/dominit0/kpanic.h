#pragma once

#include <facetos/dominit0/klog.h>
#include <sel4/sel4.h>

#define kpanic(msg) do {					\
	klog(LOG_ERROR, "KERNEL PANIC: %s\n", (msg));	\
	for (;;)						\
		seL4_Yield();					\
} while (0)

