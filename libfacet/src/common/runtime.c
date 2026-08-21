#include <facetos/libfacet/common.h>
#include <facetos/libfacet/platform.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct FacetProxyState {
    uint32_t magic;
    FacetHandle handle;
    const FacetInterfaceMeta *metadata;
} FacetProxyState;

typedef struct FacetObjectPrefix {
    void *self;
    void *priv;
    void *(*getInterface)(void *self, uuid_t iid);
} FacetObjectPrefix;

typedef struct FacetExportContext {
    void *interface_object;
    const FacetInterfaceMeta *metadata;
} FacetExportContext;

#define FACET_PROXY_MAGIC 0x46505258u

static const FacetInterfaceMeta *generic_metadata;

static FacetProxyState *proxy_state(void *interface_object)
{
    if (interface_object == NULL) {
        return NULL;
    }

    FacetObjectPrefix *prefix = interface_object;
    FacetProxyState *state = prefix->priv;
    if (state == NULL || state->magic != FACET_PROXY_MAGIC) {
        return NULL;
    }
    return state;
}

void *libfacet_new_proxy_client(
    const FacetInterfaceMeta *metadata,
    FacetHandle handle)
{
    if (metadata == NULL || metadata->interface_size == 0 ||
        metadata->initialize_proxy == NULL) {
        return NULL;
    }

    void *interface_object = calloc(1, metadata->interface_size);
    FacetProxyState *state = calloc(1, sizeof(*state));
    if (interface_object == NULL || state == NULL) {
        free(interface_object);
        free(state);
        return NULL;
    }

    state->magic = FACET_PROXY_MAGIC;
    state->handle = handle;
    state->metadata = metadata;
    metadata->initialize_proxy(interface_object, state);
    return interface_object;
}

void libfacet_free_proxy_client(void *interface_object)
{
    FacetProxyState *state = proxy_state(interface_object);
    if (state == NULL) {
        return;
    }

    state->magic = 0;
    free(state);
    free(interface_object);
}

static const FacetMethodMeta *find_method(
    const FacetInterfaceMeta *metadata,
    uint32_t method_id)
{
    if (metadata == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < metadata->method_count; i++) {
        if (metadata->methods[i].method_id == method_id) {
            return &metadata->methods[i];
        }
    }
    return NULL;
}

static int append_word(FacetRpcMessage *message, uint64_t word)
{
    if (message->word_count >= FACET_RPC_MAX_WORDS) {
        return -1;
    }
    message->words[message->word_count++] = word;
    return 0;
}

static int append_input(
    va_list *arguments,
    FacetRpcMessage *message,
    const FacetParamMeta *parameter)
{
    switch (parameter->type) {
    case FACET_TYPE_U8:
    case FACET_TYPE_U16:
    case FACET_TYPE_U32:
    case FACET_TYPE_BOOL:
        return append_word(message, (uint64_t)(unsigned int)va_arg(*arguments, int));
    case FACET_TYPE_U64:
        return append_word(message, va_arg(*arguments, uint64_t));
    case FACET_TYPE_I8:
    case FACET_TYPE_I16:
    case FACET_TYPE_I32:
        return append_word(message, (uint64_t)(int64_t)va_arg(*arguments, int));
    case FACET_TYPE_I64:
        return append_word(message, (uint64_t)va_arg(*arguments, int64_t));
    case FACET_TYPE_UUID: {
        uuid_t value = va_arg(*arguments, uuid_t);
        uint64_t words[2] = {0, 0};
        memcpy(words, value.bytes, sizeof(value.bytes));
        return append_word(message, words[0]) == 0
            ? append_word(message, words[1]) : -1;
    }
    case FACET_TYPE_LOCAL_PTR:
        return -1;
    default:
        return -1;
    }
}

static int assign_output(
    const FacetParamMeta *parameter,
    void *destination,
    const FacetRpcMessage *reply,
    size_t *reply_index,
    size_t *reply_handle_index)
{
    if (destination == NULL) {
        return -1;
    }

    if (parameter->type == FACET_TYPE_HANDLE) {
        if (*reply_handle_index >= reply->handle_count) return -1;
        *(FacetHandle *)destination = reply->handles[(*reply_handle_index)++];
        return 0;
    }
    if (*reply_index >= reply->word_count) return -1;

    uint64_t word = reply->words[(*reply_index)++];
    switch (parameter->type) {
    case FACET_TYPE_U8:
        *(uint8_t *)destination = (uint8_t)word;
        return 0;
    case FACET_TYPE_U16:
        *(uint16_t *)destination = (uint16_t)word;
        return 0;
    case FACET_TYPE_U32:
        *(uint32_t *)destination = (uint32_t)word;
        return 0;
    case FACET_TYPE_U64:
        *(uint64_t *)destination = word;
        return 0;
    case FACET_TYPE_I8:
        *(int8_t *)destination = (int8_t)word;
        return 0;
    case FACET_TYPE_I16:
        *(int16_t *)destination = (int16_t)word;
        return 0;
    case FACET_TYPE_I32:
        *(int32_t *)destination = (int32_t)word;
        return 0;
    case FACET_TYPE_I64:
        *(int64_t *)destination = (int64_t)word;
        return 0;
    case FACET_TYPE_BOOL:
        *(int *)destination = word != 0;
        return 0;
    case FACET_TYPE_LOCAL_PTR:
        *(void **)destination = (void *)(uintptr_t)word;
        return 0;
    default:
        return -1;
    }
}

FacetResult libfacet_proxy_client_call(
    void *self,
    const FacetMethodMeta *method,
    ...)
{
    FacetProxyState *state = proxy_state(self);
    if (state == NULL || method == NULL) {
        return FACET_INVALID_ARGUMENT;
    }

    FacetRpcMessage request = {
        .protocol_version = FACET_RPC_PROTOCOL_VERSION,
        .method_id = method->method_id,
    };
    void *outputs[FACET_RPC_MAX_WORDS];
    size_t output_count = 0;

    va_list arguments;
    va_start(arguments, method);
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetParamMeta *parameter = &method->parameters[i];
        if (parameter->direction != FACET_PARAM_IN) {
            if (output_count >= FACET_RPC_MAX_WORDS) {
                va_end(arguments);
                return FACET_INVALID_ARGUMENT;
            }
            outputs[output_count++] = va_arg(arguments, void *);
        } else if (append_input(&arguments, &request, parameter) != 0) {
            va_end(arguments);
            return FACET_NOT_SUPPORTED;
        }
    }
    va_end(arguments);

    FacetRpcMessage reply = {0};
    FacetResult result = libfacet_platform_call(
        state->handle, &request, &reply);
    if (result != FACET_OK) {
        return result;
    }
    if (reply.word_count == 0) {
        return FACET_PROTOCOL_ERROR;
    }

    result = (FacetResult)(int32_t)reply.words[0];
    if (result != FACET_OK) {
        return result;
    }

    size_t reply_index = 1;
    size_t reply_handle_index = 0;
    size_t output_index = 0;
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetParamMeta *parameter = &method->parameters[i];
        if (parameter->direction == FACET_PARAM_IN) {
            continue;
        }
        if (assign_output(parameter, outputs[output_index++],
                          &reply, &reply_index, &reply_handle_index) != 0) {
            return FACET_PROTOCOL_ERROR;
        }
    }
    return FACET_OK;
}

void *libfacet_proxy_client_get_interface(void *self, uuid_t iid)
{
    (void)self;
    (void)iid;
    return NULL;
}

static FacetResult common_dispatch(
    void *context,
    const FacetRpcMessage *request,
    FacetRpcMessage *reply)
{
    if (context == NULL || request == NULL || reply == NULL) {
        return FACET_INVALID_ARGUMENT;
    }

    FacetExportContext *export_context = context;
    const FacetMethodMeta *method = find_method(
        export_context->metadata, request->method_id);
    if (method == NULL || method->server_method == NULL) {
        return FACET_NO_SUCH_METHOD;
    }
    return method->server_method(export_context->interface_object,
                                 request, reply);
}

FacetResult libfacet_export_interface(
    void *interface_object,
    const FacetInterfaceMeta *metadata,
    FacetHandle *out_handle)
{
    if (interface_object == NULL || metadata == NULL || out_handle == NULL) {
        return FACET_INVALID_ARGUMENT;
    }
    FacetExportContext *export_context = calloc(1, sizeof(*export_context));
    if (export_context == NULL) {
        return FACET_OUT_OF_MEMORY;
    }
    export_context->interface_object = interface_object;
    export_context->metadata = metadata;
    FacetResult result = libfacet_platform_export(
        export_context, common_dispatch, out_handle);
    if (result != FACET_OK) {
        free(export_context);
    }
    return result;
}

FacetResult libfacet_unexport_interface(FacetHandle handle)
{
    return libfacet_platform_unexport(handle);
}

FacetResult libfacet_handle_clone(FacetHandle source, FacetHandle *destination)
{
    return libfacet_platform_handle_clone(source, destination);
}

FacetResult libfacet_handle_release(FacetHandle handle)
{
    return libfacet_platform_handle_release(handle);
}

FacetResult libfacet_get_method_handle(
    void *interface_object,
    const FacetMethodMeta *method,
    FacetHandle *out_handle)
{
    FacetProxyState *proxy = proxy_state(interface_object);
    if (proxy != NULL) {
        return libfacet_platform_method_handle(
            proxy->handle, method->method_id, out_handle);
    }
    return FACET_NOT_SUPPORTED;
}

FacetResult libfacet_register_generic_metadata(
    const FacetInterfaceMeta *metadata)
{
    if (metadata == NULL) {
        return FACET_INVALID_ARGUMENT;
    }
    generic_metadata = metadata;
    return FACET_OK;
}

IGenericObject *libfacet_proxy_from(uint64_t platform_handle)
{
    if (generic_metadata == NULL) {
        return NULL;
    }
    FacetHandle handle;
    if (libfacet_platform_handle_from(platform_handle, &handle) != FACET_OK) {
        return NULL;
    }
    return libfacet_new_proxy_client(generic_metadata, handle);
}
