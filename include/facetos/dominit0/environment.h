#pragma once

#include <facetos/dominit0/config.h>
#include <facetos/dominit0/posix.h>
#include <facetos/interfaces/IProcessEnvironment.h>

typedef struct Dominit0DomainEnvironment Dominit0DomainEnvironment;
typedef struct Dominit0ProcessEnvironment Dominit0ProcessEnvironment;

typedef enum Dominit0ProcessProfile {
    DOMINIT0_PROCESS_NATIVE = 0,
    DOMINIT0_PROCESS_PURE_POSIX = 1,
} Dominit0ProcessProfile;

int dominit0_environment_initialize(Dominit0SystemConfig *system);
void dominit0_environment_destroy(Dominit0SystemConfig *system);

int dominit0_environment_bind_page_allocator(Dominit0DomainEnvironment *environment,
                                              FacetHandle page_allocator);
int dominit0_environment_bind_file_store(Dominit0DomainEnvironment *environment,
                                         FacetInitrd *initrd,
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
FacetInitrd *dominit0_environment_initrd(
    Dominit0DomainEnvironment *environment);

Dominit0ProcessEnvironment *dominit0_process_environment_create(
    Dominit0DomainEnvironment *parent, FacetHandle session,
    bool bootstrap_authority, Dominit0ProcessProfile profile,
    const FacetArray_string *sysv_environment, FacetHandle cwd);
int dominit0_process_environment_bind_named(
    Dominit0ProcessEnvironment *environment, const char *name,
    uuid_t primary_iid, FacetHandle object);
int dominit0_process_environment_bind_page_allocator(
    Dominit0ProcessEnvironment *environment, FacetHandle allocator);
int dominit0_process_environment_bind_lifecycle(
    Dominit0ProcessEnvironment *environment, FacetHandle lifecycle);
int dominit0_process_environment_bind_terminal(
    Dominit0ProcessEnvironment *environment, FacetHandle input,
    FacetHandle output, FacetHandle control, FacetHandle terminal);
int dominit0_process_environment_set_posix_root(
    Dominit0ProcessEnvironment *environment, const char *path);
/* Returns a retained copy of the process CWD for an inherited child. */
FacetHandle dominit0_process_environment_cwd_handle(
    const Dominit0ProcessEnvironment *environment);
void dominit0_process_environment_set_posix_synthetic_cwd(
    Dominit0ProcessEnvironment *environment, bool synthetic_etc);
bool dominit0_process_environment_posix_synthetic_cwd(
    const Dominit0ProcessEnvironment *environment);
void dominit0_process_environment_set_terminal_index(
    Dominit0ProcessEnvironment *environment, uint64_t terminal_index);
int dominit0_process_environment_set_terminal_name(
    Dominit0ProcessEnvironment *environment, const char *terminal_name);
int dominit0_process_environment_bind_posix_process_control(
    Dominit0ProcessEnvironment *environment, void *context, uint64_t domain_id, int32_t pid,
    FacetHandle default_session, Dominit0PosixSpawn spawn, Dominit0PosixWait wait);
FacetHandle dominit0_process_environment_root_handle(
    const Dominit0ProcessEnvironment *environment);
int dominit0_process_environment_get_sysv(
    const Dominit0ProcessEnvironment *environment, size_t *count,
    const char *const **values);
void dominit0_process_environment_destroy(
    Dominit0ProcessEnvironment *environment);
