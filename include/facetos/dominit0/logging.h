#pragma once

#include <facetos/dominit0/config.h>

/* Constructs global sink implementations, then applies domain 0's immutable
 * ILoggingConfig while klog is still on its emergency route. */
int dominit0_logging_initialize(Dominit0SystemConfig *system);
