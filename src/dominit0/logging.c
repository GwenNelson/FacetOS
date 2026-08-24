#include <facetos/dominit0/logging.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>

#include <stdlib.h>
#include <string.h>

static KlogSinkBinding *logging_bindings;
static size_t logging_binding_count;
static bool logging_initialized;

int dominit0_logging_initialize(Dominit0SystemConfig *system)
{
    if (logging_initialized || system == NULL || system->domain_count == 0 ||
        system->root_index >= system->domain_count)
        return -1;

    logging_binding_count = system->parsed.logging_sink_count;
    if (logging_binding_count != 0) {
        logging_bindings = calloc(logging_binding_count,
                                  sizeof(*logging_bindings));
        if (logging_bindings == NULL)
            return -1;
    }

    for (size_t i = 0; i < logging_binding_count; i++) {
        const FacetConfigLoggingSinkDefinition *definition =
            &system->parsed.logging_sinks[i];
        logging_bindings[i].name.data = definition->name;
        logging_bindings[i].name.length = strlen(definition->name);
        if (platform_get_logging_sink(definition->type,
                                      &logging_bindings[i].sink) == 0)
            continue;

        if (definition->required) {
            klog(LOG_ERROR,
                 "Required logging sink %s (%s) is unavailable\n",
                 definition->name, definition->type);
            return -1;
        }
        klog(LOG_WARN,
             "Optional logging sink %s (%s) is unavailable; disabling it\n",
             definition->name, definition->type);
    }

    ILoggingConfig *root_config =
        &system->domains[system->root_index].logging;
    if (klog_init_postboot(root_config, logging_bindings,
                           logging_binding_count) != 0) {
        klog(LOG_ERROR,
             "Domain 0 logging configuration could not be activated\n");
        return -1;
    }

    logging_initialized = true;
    return 0;
}
