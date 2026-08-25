#pragma once

#include <facetos/dominit0/config.h>

typedef struct Dominit0DomainEnvironment Dominit0DomainEnvironment;
typedef struct Dominit0ProcessEnvironment Dominit0ProcessEnvironment;

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

FacetResult dominit0_environment_resolve_named(
    Dominit0DomainEnvironment *environment, const char *name,
    uuid_t *primary_iid, FacetHandle *handle);

Dominit0ProcessEnvironment *dominit0_process_environment_create(
    Dominit0DomainEnvironment *parent, FacetHandle session,
    bool bootstrap_authority);
int dominit0_process_environment_bind_named(
    Dominit0ProcessEnvironment *environment, const char *name,
    uuid_t primary_iid, FacetHandle object);
int dominit0_process_environment_bind_page_allocator(
    Dominit0ProcessEnvironment *environment, FacetHandle allocator);
FacetHandle dominit0_process_environment_root_handle(
    const Dominit0ProcessEnvironment *environment);
void dominit0_process_environment_destroy(
    Dominit0ProcessEnvironment *environment);
