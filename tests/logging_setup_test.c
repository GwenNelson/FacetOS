#include <facetos/dominit0/logging.h>
#include <facetos/dominit0/klog.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CaptureSink {
    ILoggingSink object;
    char bytes[2048];
    size_t used;
} CaptureSink;

typedef struct ConfigState {
    FacetArray_Sink sinks;
} ConfigState;

static CaptureSink emergency;
static CaptureSink debug;

void *kmalloc(size_t size) { return malloc(size); }
void *krealloc(void *pointer, size_t size) { return realloc(pointer, size); }
void kfree(void *pointer) { free(pointer); }
int kmalloc_is_in_progress(void) { return 0; }
void platform_yield(void) { abort(); }

static FacetResult capture_emit(void *self, const FacetString *data)
{
    CaptureSink *sink = self;
    assert(data != NULL);
    assert(data->length < sizeof(sink->bytes) - sink->used);
    memcpy(sink->bytes + sink->used, data->data, data->length);
    sink->used += data->length;
    sink->bytes[sink->used] = '\0';
    return FACET_OK;
}

static void capture_init(CaptureSink *sink)
{
    memset(sink, 0, sizeof(*sink));
    sink->object.self = sink;
    sink->object.emit = capture_emit;
}

int platform_get_logging_sink(const char *type, ILoggingSink **result)
{
    assert(type != NULL && result != NULL);
    *result = NULL;
    if (strcmp(type, "platform.sel4.debug") != 0)
        return -1;
    *result = &debug.object;
    return 0;
}

static FacetResult get_sinks(void *self, FacetArray_Sink *value)
{
    if (value == NULL)
        return FACET_INVALID_ARGUMENT;
    *value = ((ConfigState *)self)->sinks;
    return FACET_OK;
}

static FacetString string(const char *value)
{
    return (FacetString){ .data = value, .length = strlen(value) };
}

static void initialize_system(Dominit0SystemConfig *system,
                              Dominit0DomainConfigObject *root,
                              ConfigState *config,
                              FacetConfigLoggingSinkDefinition *definitions,
                              size_t definition_count,
                              Sink *sinks,
                              size_t sink_count)
{
    memset(system, 0, sizeof(*system));
    memset(root, 0, sizeof(*root));
    memset(config, 0, sizeof(*config));
    config->sinks.data = sinks;
    config->sinks.count = sink_count;
    root->logging.self = config;
    root->logging.getsinks = get_sinks;
    system->parsed.logging_sinks = definitions;
    system->parsed.logging_sink_count = definition_count;
    system->domains = root;
    system->domain_count = 1;
    system->root_index = 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    capture_init(&emergency);
    capture_init(&debug);
    klog_init_early(&emergency.object);

    Dominit0SystemConfig system;
    Dominit0DomainConfigObject root;
    ConfigState config;

    if (strcmp(argv[1], "required") == 0) {
        FacetConfigLoggingSinkDefinition definition = {
            .name = "missing", .type = "platform.unknown", .required = true,
        };
        initialize_system(&system, &root, &config, &definition, 1, NULL, 0);
        assert(dominit0_logging_initialize(&system) != 0);
        assert(strstr(emergency.bytes, "Required logging sink missing") != NULL);
    } else {
        assert(strcmp(argv[1], "optional") == 0);
        FacetConfigLoggingSinkDefinition definitions[] = {
            { .name = "debug", .type = "platform.sel4.debug", .required = true },
            { .name = "missing", .type = "platform.unknown", .required = false },
        };
        Sink sinks[] = {
            { .name = string("debug"), .level = LogLevel_Debug },
            { .name = string("missing"), .level = LogLevel_Debug },
        };
        initialize_system(&system, &root, &config, definitions, 2, sinks, 2);
        assert(dominit0_logging_initialize(&system) == 0);
        assert(strstr(emergency.bytes, "Optional logging sink missing") != NULL);
        assert(strstr(debug.bytes, "Switched to dynamic log buffer!") != NULL);
        klog(LOG_INFO, "configured output\n");
        assert(strstr(debug.bytes, "configured output\n") != NULL);
    }

    puts("logging setup test passed");
    return 0;
}
