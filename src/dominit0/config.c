#include <facetos/dominit0/config.h>

#include <stdlib.h>
#include <string.h>

static Dominit0SystemConfig configured_system;
static bool configured_system_initialized;

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static bool handle_is_bound(FacetHandle handle)
{
    return handle.platform != NULL;
}

static FacetResult return_handle(FacetHandle handle, FacetHandle *result)
{
    if (result == NULL)
        return FACET_INVALID_ARGUMENT;
    *result = (FacetHandle){0};
    if (!handle_is_bound(handle))
        return FACET_INVALID_HANDLE;
    *result = handle;
    return FACET_OK;
}

static FacetResult domain_get_interface(void *self, uuid_t iid,
                                        FacetHandle *result)
{
    Dominit0DomainConfigObject *object = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_IDomainConfig))
        return return_handle(object->domain_handle, result);
    if (iid_equal(iid, IID_ILoggingConfig))
        return return_handle(object->logging_handle, result);
    if (iid_equal(iid, IID_IDomainConsoleConfig))
        return return_handle(object->console_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult logging_get_interface(void *self, uuid_t iid,
                                         FacetHandle *result)
{
    Dominit0DomainConfigObject *object = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_ILoggingConfig))
        return return_handle(object->logging_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult console_get_interface(void *self, uuid_t iid,
                                         FacetHandle *result)
{
    Dominit0DomainConfigObject *object = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_IDomainConsoleConfig))
        return return_handle(object->console_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult get_domain_id(void *self, uint64_t *value)
{
    if (value == NULL) return FACET_INVALID_ARGUMENT;
    *value = ((Dominit0DomainConfigObject *)self)->domain._domain_id;
    return FACET_OK;
}

static FacetResult get_domain_name(void *self, FacetString *value)
{
    if (value == NULL) return FACET_INVALID_ARGUMENT;
    *value = ((Dominit0DomainConfigObject *)self)->domain._domain_name;
    return FACET_OK;
}

static FacetResult get_personality(void *self, Personality *value)
{
    if (value == NULL) return FACET_INVALID_ARGUMENT;
    *value = ((Dominit0DomainConfigObject *)self)->domain._personality;
    return FACET_OK;
}

static FacetResult get_logger_config(void *self, FacetHandle *value)
{
    return return_handle(((Dominit0DomainConfigObject *)self)->logging_handle,
                         value);
}

static FacetResult get_console_config(void *self, FacetHandle *value)
{
    return return_handle(((Dominit0DomainConfigObject *)self)->console_handle,
                         value);
}

static FacetResult get_domain_manager(void *self, DomainManagerMode *value)
{
    if (value == NULL) return FACET_INVALID_ARGUMENT;
    *value = ((Dominit0DomainConfigObject *)self)->domain._domain_manager;
    return FACET_OK;
}

static FacetResult get_sinks(void *self, FacetArray_Sink *value)
{
    if (value == NULL) return FACET_INVALID_ARGUMENT;
    *value = ((Dominit0DomainConfigObject *)self)->logging._sinks;
    return FACET_OK;
}

static FacetResult get_assignments(void *self, FacetArray_Assignment *value)
{
    if (value == NULL) return FACET_INVALID_ARGUMENT;
    *value = ((Dominit0DomainConfigObject *)self)->console._assignments;
    return FACET_OK;
}

static void set_diagnostic(FacetConfigDiagnostic *diagnostic,
                           const char *context, const char *message)
{
    if (diagnostic == NULL) return;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->category = FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY;
    size_t context_size = strlen(context);
    if (context_size >= sizeof(diagnostic->context))
        context_size = sizeof(diagnostic->context) - 1;
    memcpy(diagnostic->context, context, context_size);
    size_t message_size = strlen(message);
    if (message_size >= sizeof(diagnostic->message))
        message_size = sizeof(diagnostic->message) - 1;
    memcpy(diagnostic->message, message, message_size);
}

static int initialize_domain_object(Dominit0DomainConfigObject *object,
                                    const FacetSystemConfig *system,
                                    const FacetConfigDomain *source,
                                    FacetConfigDiagnostic *diagnostic)
{
    memset(object, 0, sizeof(*object));

    object->domain.self = object;
    object->domain.priv = object;
    object->domain._domain_id = source->id;
    object->domain._domain_name.data = source->name;
    object->domain._domain_name.length = strlen(source->name);
    object->domain._personality = (Personality)source->personality;
    object->domain._domain_manager = (DomainManagerMode)source->domain_manager;
    object->domain.getInterface = domain_get_interface;
    object->domain.getdomain_id = get_domain_id;
    object->domain.getdomain_name = get_domain_name;
    object->domain.getpersonality = get_personality;
    object->domain.getlogger_config = get_logger_config;
    object->domain.getconsole_config = get_console_config;
    object->domain.getdomain_manager = get_domain_manager;

    object->logging.self = object;
    object->logging.priv = object;
    object->logging.getInterface = logging_get_interface;
    object->logging.getsinks = get_sinks;
    object->logging._sinks.count = source->logging_sink_count;
    if (source->logging_sink_count != 0) {
        if (source->logging_sink_count > SIZE_MAX / sizeof(Sink)) {
            set_diagnostic(diagnostic, source->name,
                           "logging sink array size overflow");
            return -1;
        }
        object->logging._sinks.data =
            calloc(source->logging_sink_count, sizeof(Sink));
        if (object->logging._sinks.data == NULL) {
            set_diagnostic(diagnostic, source->name,
                           "could not allocate logging sink view");
            return -1;
        }
        for (size_t i = 0; i < source->logging_sink_count; i++) {
            const FacetConfigDomainSink *use = &source->logging_sinks[i];
            const FacetConfigLoggingSinkDefinition *definition =
                &system->logging_sinks[use->sink_definition_index];
            object->logging._sinks.data[i].name.data = definition->name;
            object->logging._sinks.data[i].name.length = strlen(definition->name);
            object->logging._sinks.data[i].level = (LogLevel)use->level;
        }
    }

    object->console.self = object;
    object->console.priv = object;
    object->console.getInterface = console_get_interface;
    object->console.getassignments = get_assignments;
    object->console._assignments.count = source->terminal_count;
    if (source->terminal_count != 0) {
        if (source->terminal_count > SIZE_MAX / sizeof(Assignment)) {
            set_diagnostic(diagnostic, source->name,
                           "terminal assignment array size overflow");
            return -1;
        }
        object->console._assignments.data =
            calloc(source->terminal_count, sizeof(Assignment));
        if (object->console._assignments.data == NULL) {
            set_diagnostic(diagnostic, source->name,
                           "could not allocate terminal assignment view");
            return -1;
        }
        for (size_t i = 0; i < source->terminal_count; i++) {
            const FacetConfigTerminalAssignment *assignment =
                &source->terminals[i];
            const FacetConfigSeatDefinition *seat =
                &system->seats[assignment->seat_index];
            const char *terminal = seat->terminals[assignment->terminal_index];
            object->console._assignments.data[i].seat.data = seat->name;
            object->console._assignments.data[i].seat.length = strlen(seat->name);
            object->console._assignments.data[i].terminal.data = terminal;
            object->console._assignments.data[i].terminal.length = strlen(terminal);
        }
    }
    return 0;
}

int dominit0_config_objects_init(Dominit0SystemConfig *system,
                                 FacetSystemConfig *parsed,
                                 FacetConfigDiagnostic *diagnostic)
{
    if (system == NULL || parsed == NULL || parsed->domain_count == 0)
        return -1;
    memset(system, 0, sizeof(*system));
    if (parsed->domain_count > SIZE_MAX / sizeof(*system->domains)) {
        set_diagnostic(diagnostic, "domains", "domain object array size overflow");
        return -1;
    }
    system->domains = calloc(parsed->domain_count, sizeof(*system->domains));
    if (system->domains == NULL) {
        set_diagnostic(diagnostic, "domains", "could not allocate domain objects");
        return -1;
    }
    system->domain_count = parsed->domain_count;
    system->root_index = parsed->root_index;
    system->parsed = *parsed;
    memset(parsed, 0, sizeof(*parsed));
    for (size_t i = 0; i < system->domain_count; i++) {
        if (initialize_domain_object(&system->domains[i], &system->parsed,
                                     &system->parsed.domains[i],
                                     diagnostic) != 0) {
            dominit0_config_objects_destroy(system);
            return -1;
        }
    }
    return 0;
}

void dominit0_config_objects_destroy(Dominit0SystemConfig *system)
{
    if (system == NULL) return;
    for (size_t i = 0; i < system->domain_count; i++) {
        free(system->domains[i].logging._sinks.data);
        free(system->domains[i].console._assignments.data);
    }
    free(system->domains);
    facet_config_destroy(&system->parsed);
    memset(system, 0, sizeof(*system));
}

int dominit0_domain_config_bind_handles(Dominit0DomainConfigObject *object,
                                        FacetHandle domain_handle,
                                        FacetHandle logging_handle,
                                        FacetHandle console_handle)
{
    if (object == NULL || !handle_is_bound(domain_handle) ||
        !handle_is_bound(logging_handle) || !handle_is_bound(console_handle))
        return -1;
    object->domain_handle = domain_handle;
    object->logging_handle = logging_handle;
    object->console_handle = console_handle;
    object->domain._logger_config = logging_handle;
    object->domain._console_config = console_handle;
    return 0;
}

int dominit0_config_initialize(const uint8_t *data, size_t size,
                               bool source_present,
                               FacetConfigDiagnostic *diagnostic)
{
    if (configured_system_initialized)
        return -1;
    FacetSystemConfig parsed;
    int result = source_present
        ? facet_config_parse(data, size, &parsed, diagnostic)
        : facet_config_make_fallback(&parsed, diagnostic);
    if (result != 0)
        return -1;
    if (dominit0_config_objects_init(&configured_system, &parsed,
                                     diagnostic) != 0) {
        facet_config_destroy(&parsed);
        return -1;
    }
    configured_system_initialized = true;
    return 0;
}

Dominit0SystemConfig *dominit0_config_get_system(void)
{
    return configured_system_initialized ? &configured_system : NULL;
}
