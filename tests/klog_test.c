#include <facetos/dominit0/klog.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CaptureSink {
    ILoggingSink object;
    char bytes[4096];
    size_t used;
    size_t calls;
    bool fail;
} CaptureSink;

static char *retained_buffer;

void *kmalloc(size_t size)
{
    retained_buffer = malloc(size);
    return retained_buffer;
}

void *krealloc(void *pointer, size_t size)
{
    retained_buffer = realloc(pointer, size);
    return retained_buffer;
}

void kfree(void *pointer)
{
    free(pointer);
}

int kmalloc_is_in_progress(void)
{
    return 0;
}

void platform_yield(void)
{
    abort();
}

static FacetResult capture_emit(void *self, const FacetString *data)
{
    CaptureSink *sink = self;
    sink->calls++;
    if (sink->fail)
        return FACET_ERROR;
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

typedef struct TestConfig {
    ILoggingConfig object;
    FacetArray_Sink sinks;
} TestConfig;

static FacetResult get_sinks(void *self, FacetArray_Sink *value)
{
    if (value == NULL)
        return FACET_INVALID_ARGUMENT;
    *value = ((TestConfig *)self)->sinks;
    return FACET_OK;
}

static FacetString string(const char *value)
{
    return (FacetString){ .data = value, .length = strlen(value) };
}

static void config_init(TestConfig *config, Sink *sinks, size_t count)
{
    memset(config, 0, sizeof(*config));
    config->object.self = config;
    config->object.getsinks = get_sinks;
    config->sinks.data = sinks;
    config->sinks.count = count;
}

static bool contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

int main(void)
{
    CaptureSink emergency, debug, info, disabled, failing;
    capture_init(&emergency);
    capture_init(&debug);
    capture_init(&info);
    capture_init(&disabled);
    capture_init(&failing);
    failing.fail = true;

    klog_init_early(&emergency.object);
    klog(LOG_INFO, "early record\n");
    assert(contains(emergency.bytes, "early record\n"));
    size_t emergency_before_switch = emergency.used;

    Sink missing_sink = { .name = string("missing"), .level = LogLevel_Info };
    TestConfig missing_config;
    config_init(&missing_config, &missing_sink, 1);
    assert(klog_init_postboot(&missing_config.object, NULL, 0) != 0);

    Sink too_many[9] = {0};
    TestConfig excessive_config;
    config_init(&excessive_config, too_many,
                sizeof(too_many) / sizeof(too_many[0]));
    assert(klog_init_postboot(&excessive_config.object, NULL, 0) != 0);

    Sink configured_sinks[] = {
        { .name = string("debug"), .level = LogLevel_Debug },
        { .name = string("info"), .level = LogLevel_Info },
        { .name = string("disabled"), .level = LogLevel_None },
        { .name = string("optional"), .level = LogLevel_Debug },
        { .name = string("failing"), .level = LogLevel_Warning },
    };
    TestConfig config;
    config_init(&config, configured_sinks,
                sizeof(configured_sinks) / sizeof(configured_sinks[0]));
    KlogSinkBinding bindings[] = {
        { .name = string("debug"), .sink = &debug.object },
        { .name = string("info"), .sink = &info.object },
        { .name = string("disabled"), .sink = &disabled.object },
        { .name = string("optional"), .sink = NULL },
        { .name = string("failing"), .sink = &failing.object },
    };

    assert(klog_init_postboot(&config.object, bindings,
                              sizeof(bindings) / sizeof(bindings[0])) == 0);
    assert(emergency.used == emergency_before_switch);
    assert(strncmp(retained_buffer, "early record\n",
                   strlen("early record\n")) == 0);
    assert(contains(retained_buffer, "Switched to dynamic log buffer!\n"));
    assert(contains(debug.bytes, "Switched to dynamic log buffer!\n"));
    assert(contains(info.bytes, "Switched to dynamic log buffer!\n"));
    assert(disabled.calls == 0);
    assert(failing.calls == 0);

    debug.used = debug.calls = 0;
    info.used = info.calls = 0;
    klog(LOG_DEBUG, "debug-only\n");
    assert(contains(debug.bytes, "debug-only\n"));
    assert(!contains(info.bytes, "debug-only\n"));

    klog(LOG_INFO, "info-route\n");
    assert(contains(debug.bytes, "info-route\n"));
    assert(contains(info.bytes, "info-route\n"));

    klog(LOG_WARN, "warning-route\n");
    assert(failing.calls == 1);
    assert(contains(emergency.bytes, "configured sink failed"));
    assert(contains(emergency.bytes, "warning-route\n"));
    klog(LOG_ERROR, "error-route\n");
    assert(failing.calls == 1);
    assert(contains(debug.bytes, "error-route\n"));
    assert(contains(info.bytes, "error-route\n"));
    assert(disabled.calls == 0);
    assert(contains(retained_buffer, "debug-only\n"));
    assert(contains(retained_buffer, "error-route\n"));

    puts("klog tests passed");
    return 0;
}
