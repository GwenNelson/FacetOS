#include <facetos/dominit0/logging.h>
#include <facetos/dominit0/klock.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>

#include <stdlib.h>
#include <string.h>

static KlogSinkBinding *logging_bindings;
static size_t logging_binding_count;
static bool logging_initialized;
static klock_t logging_emit_lock = KLOCK_INITIALIZER;

#define DOMAIN_LOG_MAX_RECORD 512

static bool string_equal(FacetString left, FacetString right)
{
    return left.length == right.length &&
        (left.length == 0 || (left.data != NULL && right.data != NULL &&
                              memcmp(left.data, right.data, left.length) == 0));
}

static bool valid_level(int32_t level)
{
    return level == LogLevel_None || level == LogLevel_Fatal ||
        level == LogLevel_Error || level == LogLevel_Warning ||
        level == LogLevel_Info || level == LogLevel_Debug ||
        level == LogLevel_Trace;
}

static int append_char(char *buffer, size_t *used, char value)
{
    if (*used == DOMAIN_LOG_MAX_RECORD)
        return -1;
    buffer[(*used)++] = value;
    return 0;
}

static int append_string(char *buffer, size_t *used, FacetString value)
{
    if ((value.length != 0 && value.data == NULL) ||
        value.length > DOMAIN_LOG_MAX_RECORD - *used)
        return -1;
    memcpy(buffer + *used, value.data, value.length);
    *used += value.length;
    return 0;
}

static int append_uint(char *buffer, size_t *used, uint64_t value)
{
    char digits[sizeof(value) * 3];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0 && append_char(buffer, used, digits[--count]) != 0)
        return -1;
    return 0;
}

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
    klock_init(&logging_emit_lock);
    return 0;
}

FacetResult dominit0_logging_emit(ILoggingConfig *config, uint64_t domain_id,
                                  int32_t level, FacetString component,
                                  FacetString message)
{
    if (!logging_initialized || config == NULL || config->getsinks == NULL ||
        !valid_level(level) || (component.length != 0 && component.data == NULL) ||
        (message.length != 0 && message.data == NULL))
        return FACET_INVALID_ARGUMENT;

    char record[DOMAIN_LOG_MAX_RECORD];
    size_t used = 0;
    static const FacetString prefix = { .data = "[domain ", .length = 8 };
    static const FacetString separator = { .data = "] ", .length = 2 };
    static const FacetString colon = { .data = ": ", .length = 2 };
    if (append_string(record, &used, prefix) != 0 ||
        append_uint(record, &used, domain_id) != 0 ||
        append_string(record, &used, separator) != 0 ||
        append_string(record, &used, component) != 0 ||
        append_string(record, &used, colon) != 0 ||
        append_string(record, &used, message) != 0 ||
        append_char(record, &used, '\n') != 0)
        return FACET_BUFFER_TOO_SMALL;

    FacetArray_Sink routes = {0};
    if (config->getsinks(config->self, &routes) != FACET_OK)
        return FACET_ERROR;

    FacetString output = { .data = record, .length = used };
    FacetResult result = FACET_OK;
    klock_lock(&logging_emit_lock);
    for (size_t i = 0; i < routes.count; i++) {
        if ((int32_t)routes.data[i].level < level ||
            routes.data[i].level == LogLevel_None)
            continue;
        for (size_t j = 0; j < logging_binding_count; j++) {
            if (!string_equal(routes.data[i].name, logging_bindings[j].name) ||
                logging_bindings[j].sink == NULL)
                continue;
            if (logging_bindings[j].sink->emit(
                    logging_bindings[j].sink->self, &output) != FACET_OK)
                result = FACET_ERROR;
            break;
        }
    }
    klock_unlock(&logging_emit_lock);
    return result;
}
