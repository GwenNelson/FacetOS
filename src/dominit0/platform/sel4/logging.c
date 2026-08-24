#include <facetos/dominit0/platform/api.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/ILoggingSink.h>

#include <string.h>

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult sel4_debug_get_interface(void *self, uuid_t iid,
                                            FacetHandle *result)
{
    (void)self;
    if (result == NULL)
        return FACET_INVALID_ARGUMENT;
    *result = (FacetHandle){0};
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_ILoggingSink))
        return FACET_INVALID_HANDLE;
    return FACET_NO_INTERFACE;
}

static FacetResult sel4_debug_emit(void *self, const FacetString *data)
{
    (void)self;
    if (data == NULL || (data->data == NULL && data->length != 0))
        return FACET_INVALID_ARGUMENT;

    char buffer[128];
    size_t offset = 0;
    while (offset < data->length) {
        size_t count = data->length - offset;
        if (count >= sizeof(buffer))
            count = sizeof(buffer) - 1;
        memcpy(buffer, data->data + offset, count);
        buffer[count] = '\0';
        platform_debug_print(buffer);
        offset += count;
    }
    return FACET_OK;
}

static ILoggingSink sel4_debug_sink = {
    .self = &sel4_debug_sink,
    .priv = NULL,
    .getInterface = sel4_debug_get_interface,
    .emit = sel4_debug_emit,
};

ILoggingSink *platform_get_early_logging_sink(void)
{
    return &sel4_debug_sink;
}

int platform_get_logging_sink(const char *type, ILoggingSink **result)
{
    if (type == NULL || result == NULL)
        return -1;
    *result = NULL;
    if (strcmp(type, "platform.sel4.debug") != 0)
        return -1;
    *result = &sel4_debug_sink;
    return 0;
}
