#pragma once

#include <facetos/dominit0/config.h>

/* Creates the immutable local authentication authority from facet.toml and
 * delegates it only to domains selecting the local authentication source. */
int dominit0_auth_initialize(Dominit0SystemConfig *system);
void dominit0_auth_destroy(void);

/* Trusted bootstrap path for a terminal assignment's configured run_as user.
 * This is intentionally not exported through the public security interface. */
FacetResult dominit0_auth_session_for_user(uint64_t domain_id,
                                           const char *name,
                                           FacetHandle *session);
FacetResult dominit0_authenticate_user(uint64_t domain_id,
                                       const FacetString *name,
                                       const FacetString *password,
                                       FacetHandle *session);
