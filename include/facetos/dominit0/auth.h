#pragma once

#include <facetos/dominit0/config.h>

/* Creates the immutable local authentication authority from facet.toml and
 * delegates it only to domains selecting the local authentication source. */
int dominit0_auth_initialize(Dominit0SystemConfig *system);
void dominit0_auth_destroy(void);
