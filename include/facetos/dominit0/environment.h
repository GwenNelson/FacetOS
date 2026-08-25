#pragma once

#include <facetos/dominit0/config.h>

typedef struct Dominit0DomainEnvironment Dominit0DomainEnvironment;

int dominit0_environment_initialize(Dominit0SystemConfig *system);
void dominit0_environment_destroy(Dominit0SystemConfig *system);

int dominit0_environment_bind_page_allocator(Dominit0DomainEnvironment *environment,
                                              FacetHandle page_allocator);
FacetHandle dominit0_environment_root_handle(
    const Dominit0DomainEnvironment *environment);
