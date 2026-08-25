#pragma once

#include <facetos/dominit0/config.h>

#include <facetos/interfaces/ILoggingConfig.h>

/* Constructs global sink implementations, then applies domain 0's immutable
 * ILoggingConfig while klog is still on its emergency route. */
int dominit0_logging_initialize(Dominit0SystemConfig *system);

FacetResult dominit0_logging_emit(ILoggingConfig *config, uint64_t domain_id,
                                  int32_t level, FacetString component,
                                  FacetString message);
