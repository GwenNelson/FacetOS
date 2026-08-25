#pragma once

#include <facetos/dominit0/config.h>

typedef struct Dominit0DomainEnvironment Dominit0DomainEnvironment;

int dominit0_environment_initialize(Dominit0SystemConfig *system);
void dominit0_environment_destroy(Dominit0SystemConfig *system);

int dominit0_environment_bind_page_allocator(Dominit0DomainEnvironment *environment,
                                              FacetHandle page_allocator);
int dominit0_environment_bind_file_store(Dominit0DomainEnvironment *environment,
                                         FacetHandle file_store);
/* Delegates one explicitly named process-local capability.  The environment
 * takes a private copy of name but never owns the exported object itself. */
int dominit0_environment_bind_named(Dominit0DomainEnvironment *environment,
                                    const char *name, uuid_t primary_iid,
                                    FacetHandle object);
FacetHandle dominit0_environment_root_handle(
    const Dominit0DomainEnvironment *environment);
