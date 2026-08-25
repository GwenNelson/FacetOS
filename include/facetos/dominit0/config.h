#pragma once

#include <facetos/config.h>
#include <facetos/interfaces/IDomainConfig.h>

typedef struct Dominit0DomainEnvironment Dominit0DomainEnvironment;
typedef struct FacetInitrd FacetInitrd;
typedef struct CurrentSeat CurrentSeat;

typedef struct Dominit0DomainConfigObject {
    IDomainConfig domain;
    ILoggingConfig logging;
    IDomainConsoleConfig console;

    FacetHandle domain_handle;
    FacetHandle logging_handle;
    FacetHandle console_handle;
} Dominit0DomainConfigObject;

/* Runtime state for one configured domain.
 *
 * This deliberately contains no platform-specific type.  The platform owns
 * and interprets platform_state after platform_start_domain() succeeds. */
typedef struct CurrentDomain {
    IDomainConfig *config;
    const FacetConfigDomain *parsed;
    Dominit0DomainEnvironment *environment;
    FacetInitrd *initrd;
    void *platform_state;
} CurrentDomain;

typedef struct Dominit0SystemConfig {
    FacetSystemConfig parsed;
    size_t domain_count;
    size_t root_index;
    Dominit0DomainConfigObject *domains;
    CurrentDomain **current_domains;
    CurrentSeat *current_seats;
} Dominit0SystemConfig;

/* Takes ownership of parsed on success and clears it. */
int dominit0_config_objects_init(Dominit0SystemConfig *system,
                                 FacetSystemConfig *parsed,
                                 FacetConfigDiagnostic *diagnostic);

void dominit0_config_objects_destroy(Dominit0SystemConfig *system);

/* Records handles created by future export code; this function exports none. */
int dominit0_domain_config_bind_handles(Dominit0DomainConfigObject *object,
                                        FacetHandle domain_handle,
                                        FacetHandle logging_handle,
                                        FacetHandle console_handle);

/* Export immutable configuration views once the platform RPC service exists. */
int dominit0_config_export_objects(Dominit0SystemConfig *system);

int dominit0_config_initialize(const uint8_t *data, size_t size,
                               bool source_present,
                               FacetConfigDiagnostic *diagnostic);

Dominit0SystemConfig *dominit0_config_get_system(void);
