#pragma once

#include <facetos/config.h>
#include <facetos/interfaces/IDomainConfig.h>

typedef struct Dominit0DomainConfigObject {
    IDomainConfig domain;
    ILoggingConfig logging;
    IDomainConsoleConfig console;

    FacetHandle domain_handle;
    FacetHandle logging_handle;
    FacetHandle console_handle;
} Dominit0DomainConfigObject;

typedef struct Dominit0SystemConfig {
    FacetSystemConfig parsed;
    size_t domain_count;
    size_t root_index;
    Dominit0DomainConfigObject *domains;
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

int dominit0_config_initialize(const uint8_t *data, size_t size,
                               bool source_present,
                               FacetConfigDiagnostic *diagnostic);

Dominit0SystemConfig *dominit0_config_get_system(void);
