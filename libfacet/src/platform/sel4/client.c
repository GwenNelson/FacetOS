#include <facetos/libfacet/platform/sel4/client.h>
#include <facetos/libfacet/platform.h>

#include <sel4/sel4.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ClientHandle {
    seL4_CPtr endpoint;
    int owns_cap;
} ClientHandle;

static seL4_CPtr client_receive_cnode;
static seL4_CPtr client_receive_slot;
static seL4_Word client_receive_depth;
static int client_ready;

static ClientHandle *as_client_handle(FacetHandle handle)
{
    return handle.platform;
}

FacetResult facet_sel4_client_init(
    uint64_t receive_cnode,
    uint64_t receive_slot,
    uint64_t receive_depth)
{
    if (receive_cnode == seL4_CapNull || receive_cnode > UINT32_MAX ||
        receive_slot == seL4_CapNull || receive_slot > UINT32_MAX ||
        receive_depth == 0 || receive_depth > seL4_WordBits) {
        return FACET_INVALID_ARGUMENT;
    }

    client_receive_cnode = (seL4_CPtr)receive_cnode;
    client_receive_slot = (seL4_CPtr)receive_slot;
    client_receive_depth = (seL4_Word)receive_depth;
    client_ready = 1;
    return FACET_OK;
}

FacetResult facet_sel4_client_yield(void)
{
    if (!client_ready) return FACET_INVALID_ARGUMENT;
    seL4_Yield();
    return FACET_OK;
}

FacetResult libfacet_platform_handle_from(
    uint64_t value,
    FacetHandle *out_handle)
{
    if (!client_ready || out_handle == NULL || value == seL4_CapNull) {
        return FACET_INVALID_ARGUMENT;
    }

    ClientHandle *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) return FACET_OUT_OF_MEMORY;

    handle->endpoint = (seL4_CPtr)value;
    handle->owns_cap = 0;
    out_handle->platform = handle;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_clone(
    FacetHandle source,
    FacetHandle *destination)
{
    ClientHandle *handle = as_client_handle(source);
    if (!client_ready || handle == NULL || destination == NULL) {
        return FACET_INVALID_HANDLE;
    }

    ClientHandle *copy = calloc(1, sizeof(*copy));
    if (copy == NULL) return FACET_OUT_OF_MEMORY;
    *copy = *handle;
    copy->owns_cap = 0;
    destination->platform = copy;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_release(FacetHandle handle)
{
    ClientHandle *native = as_client_handle(handle);
    if (native == NULL) return FACET_INVALID_HANDLE;

    if (native->owns_cap) {
        seL4_Error error = seL4_CNode_Delete(
            client_receive_cnode,
            native->endpoint,
            client_receive_depth);
        if (error != seL4_NoError) {
            free(native);
            return FACET_ERROR;
        }
    }

    free(native);
    return FACET_OK;
}

FacetResult libfacet_platform_call(
    FacetHandle target,
    const FacetRpcMessage *request,
    FacetRpcMessage *reply)
{
    ClientHandle *native = as_client_handle(target);
    if (!client_ready || native == NULL || request == NULL || reply == NULL) {
        return FACET_INVALID_ARGUMENT;
    }
    if (request->payload_size != 0 || request->attachment_count != 0 ||
        request->word_count > FACET_RPC_MAX_WORDS - 4) {
        return FACET_NOT_SUPPORTED;
    }

    seL4_Word length = 4 + request->word_count;
    for (size_t i = 0; i < request->word_count; i++) {
        seL4_SetMR((int)(4 + i), (seL4_Word)request->words[i]);
    }
    seL4_SetMR(0, request->method_id);
    seL4_SetMR(1, request->protocol_version);
    seL4_SetMR(2, request->flags);
    seL4_SetMR(3, request->payload_size);

    seL4_SetCapReceivePath(
        client_receive_cnode,
        client_receive_slot,
        client_receive_depth);

    seL4_MessageInfo_t info = seL4_MessageInfo_new(
        0, 0, 0, length);
    seL4_MessageInfo_t received = seL4_Call(native->endpoint, info);

    size_t received_length = seL4_MessageInfo_get_length(received);
    if (received_length < 4) return FACET_PROTOCOL_ERROR;

    reply->protocol_version = seL4_GetMR(1);
    reply->flags = seL4_GetMR(2);
    reply->payload_size = seL4_GetMR(3);
    reply->payload = NULL;
    reply->payload_capacity = 0;
    reply->attachment_count = 0;
    /* MR0 is the FacetResult; the remaining words begin at MR4. */
    reply->word_count = received_length - 3;
    if (reply->word_count > FACET_RPC_MAX_WORDS) {
        return FACET_PROTOCOL_ERROR;
    }
    reply->words[0] = seL4_GetMR(0);
    for (size_t i = 1; i < reply->word_count; i++) {
        reply->words[i] = seL4_GetMR((int)(4 + i - 1));
    }

    if (reply->protocol_version != FACET_RPC_PROTOCOL_VERSION ||
        reply->payload_size != 0 ||
        seL4_MessageInfo_get_extraCaps(received) > 1) {
        return FACET_PROTOCOL_ERROR;
    }

    if (seL4_MessageInfo_get_extraCaps(received) == 1) {
        ClientHandle *returned = calloc(1, sizeof(*returned));
        if (returned == NULL) return FACET_OUT_OF_MEMORY;
        /* seL4 installs a transferred capability in the receive path; the
         * caps-or-badges IPC-buffer field is not its destination CPtr. */
        returned->endpoint = client_receive_slot;
        returned->owns_cap = 1;
        reply->attachments[0].kind = FACET_RPC_ATTACHMENT_HANDLE;
        reply->attachments[0].handle.platform = returned;
        reply->attachment_count = 1;
        client_receive_slot++;
    }

    return FACET_OK;
}

FacetResult libfacet_platform_export(
    void *context,
    FacetPlatformDispatch dispatch,
    FacetHandle *out_handle)
{
    (void)context;
    (void)dispatch;
    (void)out_handle;
    return FACET_NOT_SUPPORTED;
}

FacetResult libfacet_platform_unexport(FacetHandle handle)
{
    (void)handle;
    return FACET_NOT_SUPPORTED;
}

FacetResult libfacet_platform_method_handle(
    FacetHandle object,
    uint32_t method_id,
    FacetHandle *out_handle)
{
    (void)object;
    (void)method_id;
    (void)out_handle;
    return FACET_NOT_SUPPORTED;
}

FacetResult facet_sel4_handle_get_cap(
    FacetHandle handle,
    seL4_CPtr *out_cap)
{
    (void)handle;
    (void)out_cap;
    return FACET_NOT_SUPPORTED;
}

FacetResult facet_sel4_handle_from_cap(
    seL4_CPtr cap,
    FacetHandle *out_handle)
{
    return libfacet_platform_handle_from((uint64_t)cap, out_handle);
}
