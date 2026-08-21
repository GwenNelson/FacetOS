#include <facetos/libfacet/platform/sel4.h>

#include <sel4utils/thread.h>
#include <sel4utils/thread_config.h>

#include <stdint.h>
#include <stdlib.h>

typedef struct FacetSel4Handle FacetSel4Handle;
typedef struct FacetSel4Export FacetSel4Export;

struct FacetSel4Handle {
    seL4_CPtr endpoint;
    uint32_t method_id;
    FacetSel4Export *export_state;
};

struct FacetSel4Export {
    vka_object_t endpoint;
    sel4utils_thread_t thread;
    void *context;
    FacetPlatformDispatch dispatch;
};

static FacetSel4PlatformConfig platform_config;
static int platform_ready;

static FacetSel4Handle *as_handle(FacetHandle handle)
{
    return (FacetSel4Handle *)handle.platform;
}

static FacetResult message_from_request(
    const FacetRpcMessage *request,
    seL4_MessageInfo_t *info)
{
    if (request == NULL || info == NULL ||
        request->word_count > FACET_RPC_MAX_WORDS ||
        request->word_count + 2 > seL4_MsgMaxLength) {
        return FACET_INVALID_ARGUMENT;
    }

    seL4_SetMR(0, request->method_id);
    seL4_SetMR(1, request->flags);
    for (size_t i = 0; i < request->word_count; i++) {
        seL4_SetMR(i + 2, (seL4_Word)request->words[i]);
    }
    *info = seL4_MessageInfo_new(
        0, 0, 0, (seL4_Word)(request->word_count + 2));
    return FACET_OK;
}

static void request_from_message(
    seL4_MessageInfo_t info,
    FacetRpcMessage *request)
{
    request->protocol_version = FACET_RPC_PROTOCOL_VERSION;
    request->method_id = seL4_GetMR(0);
    request->flags = seL4_GetMR(1);
    request->word_count = 0;

    seL4_Word length = seL4_MessageInfo_get_length(info);
    for (seL4_Word i = 2; i < length &&
         request->word_count < FACET_RPC_MAX_WORDS; i++) {
        request->words[request->word_count++] = seL4_GetMR(i);
    }
}

static seL4_MessageInfo_t reply_to_message(const FacetRpcMessage *reply)
{
    size_t length = reply->word_count + 1;
    if (length > seL4_MsgMaxLength) {
        length = seL4_MsgMaxLength;
    }
    for (size_t i = 0; i < length; i++) {
        seL4_SetMR(i, (seL4_Word)reply->words[i]);
    }
    return seL4_MessageInfo_new(0, 0, 0, (seL4_Word)length);
}

static void facet_sel4_server_entry(
    void *arg0,
    void *arg1,
    void *ipc_buffer)
{
    (void)arg1;
    (void)ipc_buffer;
    FacetSel4Export *export_state = arg0;

    for (;;) {
        seL4_Word badge = 0;
        seL4_MessageInfo_t received =
            seL4_Recv(export_state->endpoint.cptr, &badge);
        FacetRpcMessage request = {0};
        FacetRpcMessage reply = {0};
        request_from_message(received, &request);
        reply.word_count = 0;

        FacetResult result = export_state->dispatch(
            export_state->context, &request, &reply);
        if (reply.word_count == 0) {
            reply.words[0] = (uint64_t)(int64_t)result;
            reply.word_count = 1;
        }
        seL4_Reply(reply_to_message(&reply));
    }
}

FacetResult facet_sel4_platform_init(
    const FacetSel4PlatformConfig *config)
{
    if (config == NULL || config->vka == NULL ||
        config->vspace == NULL || config->simple == NULL) {
        return FACET_INVALID_ARGUMENT;
    }
    platform_config = *config;
    platform_ready = 1;
    return FACET_OK;
}

FacetResult facet_sel4_handle_get_cap(
    FacetHandle handle,
    seL4_CPtr *out_cap)
{
    FacetSel4Handle *native = as_handle(handle);
    if (native == NULL || out_cap == NULL) {
        return FACET_INVALID_HANDLE;
    }
    *out_cap = native->endpoint;
    return FACET_OK;
}

FacetResult facet_sel4_handle_from_cap(
    seL4_CPtr cap,
    FacetHandle *out_handle)
{
    if (cap == seL4_CapNull || out_handle == NULL) {
        return FACET_INVALID_ARGUMENT;
    }
    FacetSel4Handle *native = calloc(1, sizeof(*native));
    if (native == NULL) {
        return FACET_OUT_OF_MEMORY;
    }
    native->endpoint = cap;
    out_handle->platform = native;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_from(
    uint64_t value,
    FacetHandle *out_handle)
{
    return facet_sel4_handle_from_cap((seL4_CPtr)value, out_handle);
}

FacetResult libfacet_platform_handle_clone(
    FacetHandle source,
    FacetHandle *destination)
{
    FacetSel4Handle *native = as_handle(source);
    if (native == NULL || destination == NULL) {
        return FACET_INVALID_HANDLE;
    }
    FacetSel4Handle *copy = calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return FACET_OUT_OF_MEMORY;
    }
    *copy = *native;
    destination->platform = copy;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_release(FacetHandle handle)
{
    FacetSel4Handle *native = as_handle(handle);
    if (native == NULL) {
        return FACET_INVALID_HANDLE;
    }
    free(native);
    return FACET_OK;
}

FacetResult libfacet_platform_call(
    FacetHandle target,
    const FacetRpcMessage *request,
    FacetRpcMessage *reply)
{
    FacetSel4Handle *native = as_handle(target);
    if (!platform_ready || native == NULL || request == NULL ||
        reply == NULL) {
        return FACET_INVALID_ARGUMENT;
    }

    FacetRpcMessage actual = *request;
    if (native->method_id != 0) {
        actual.method_id = native->method_id;
    }

    seL4_MessageInfo_t info;
    FacetResult result = message_from_request(&actual, &info);
    if (result != FACET_OK) {
        return result;
    }
    seL4_MessageInfo_t received = seL4_Call(native->endpoint, info);
    reply->word_count = 0;
    seL4_Word length = seL4_MessageInfo_get_length(received);
    for (seL4_Word i = 0; i < length &&
         reply->word_count < FACET_RPC_MAX_WORDS; i++) {
        reply->words[reply->word_count++] = seL4_GetMR(i);
    }
    return FACET_OK;
}

FacetResult libfacet_platform_export(
    void *context,
    FacetPlatformDispatch dispatch,
    FacetHandle *out_handle)
{
    if (!platform_ready || context == NULL || dispatch == NULL ||
        out_handle == NULL) {
        return FACET_INVALID_ARGUMENT;
    }

    FacetSel4Export *export_state = calloc(1, sizeof(*export_state));
    FacetSel4Handle *handle = calloc(1, sizeof(*handle));
    if (export_state == NULL || handle == NULL) {
        free(export_state);
        free(handle);
        return FACET_OUT_OF_MEMORY;
    }

    int error = vka_alloc_endpoint(platform_config.vka,
                                   &export_state->endpoint);
    if (error != 0) {
        free(export_state);
        free(handle);
        return FACET_ERROR;
    }

    seL4_Word cspace_data = 0;
    sel4utils_thread_config_t thread_config = thread_config_default(
        platform_config.simple,
        simple_get_cnode(platform_config.simple),
        cspace_data,
        seL4_CapNull,
        seL4_MaxPrio);
    error = sel4utils_configure_thread_config(
        platform_config.vka, platform_config.vspace, platform_config.vspace,
        thread_config, &export_state->thread);
    if (error != 0) {
        vka_free_object(platform_config.vka, &export_state->endpoint);
        free(export_state);
        free(handle);
        return FACET_ERROR;
    }

    export_state->context = context;
    export_state->dispatch = dispatch;
    error = sel4utils_start_thread(
        &export_state->thread,
        facet_sel4_server_entry,
        export_state, NULL, 1);
    if (error != 0) {
        sel4utils_clean_up_thread(platform_config.vka,
                                  platform_config.vspace,
                                  &export_state->thread);
        vka_free_object(platform_config.vka, &export_state->endpoint);
        free(export_state);
        free(handle);
        return FACET_ERROR;
    }

    handle->endpoint = export_state->endpoint.cptr;
    handle->export_state = export_state;
    out_handle->platform = handle;
    return FACET_OK;
}

FacetResult libfacet_platform_unexport(FacetHandle handle)
{
    FacetSel4Handle *native = as_handle(handle);
    if (native == NULL || native->export_state == NULL) {
        return FACET_INVALID_HANDLE;
    }

    FacetSel4Export *export_state = native->export_state;
    seL4_TCB_Suspend(export_state->thread.tcb.cptr);
    sel4utils_clean_up_thread(platform_config.vka,
                              platform_config.vspace,
                              &export_state->thread);
    vka_free_object(platform_config.vka, &export_state->endpoint);
    free(export_state);
    native->export_state = NULL;
    return FACET_OK;
}

FacetResult libfacet_platform_method_handle(
    FacetHandle object,
    uint32_t method_id,
    FacetHandle *out_handle)
{
    FacetSel4Handle *native = as_handle(object);
    if (native == NULL || out_handle == NULL) {
        return FACET_INVALID_HANDLE;
    }
    FacetResult result = libfacet_platform_handle_clone(object, out_handle);
    if (result == FACET_OK) {
        as_handle(*out_handle)->method_id = method_id;
    }
    return result;
}
