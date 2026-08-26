#include <facetos/libfacet/platform.h>
#include <facetos/libfacet/platform/sel4/service.h>

#include <sel4/sel4.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SERVICE_EXPORT_MAX 64u

typedef struct ServiceHandle {
    /* Keep the prefix identical to client.c's ClientHandle.  A process that
     * both consumes and exports Facet objects must be able to forward either
     * kind of handle as an IPC attachment. */
    seL4_CPtr endpoint;
    int owns_cap;
    uint32_t object_id;
} ServiceHandle;

typedef struct ServiceExport {
    void *context;
    FacetPlatformDispatch dispatch;
    seL4_CPtr handle_cap;
} ServiceExport;

static seL4_CPtr service_endpoint;
static seL4_CPtr service_cnode;
static seL4_CPtr service_receive_slot;
static seL4_CPtr service_next_export_slot;
static seL4_Word service_depth;
static ServiceExport service_exports[SERVICE_EXPORT_MAX];
static size_t service_export_count;
static int service_ready;

static size_t wire_words(size_t bytes)
{
    return (bytes + sizeof(seL4_Word) - 1) / sizeof(seL4_Word);
}

static ServiceHandle *as_handle(FacetHandle handle)
{
    return handle.platform;
}

FacetResult facet_sel4_service_handle_cap(FacetHandle handle,
                                          seL4_CPtr *out_cap)
{
    ServiceHandle *native = as_handle(handle);
    if (native == NULL || out_cap == NULL) return FACET_INVALID_HANDLE;
    *out_cap = native->endpoint;
    return FACET_OK;
}

FacetResult facet_sel4_service_init(uint64_t endpoint, uint64_t cnode,
                                    uint64_t receive_slot,
                                    uint64_t first_export_slot,
                                    uint64_t depth)
{
    if (endpoint == 0 || endpoint > UINT32_MAX || cnode == 0 ||
        cnode > UINT32_MAX || receive_slot == 0 || receive_slot > UINT32_MAX ||
        first_export_slot == 0 || first_export_slot > UINT32_MAX ||
        depth == 0 || depth > seL4_WordBits)
        return FACET_INVALID_ARGUMENT;
    service_endpoint = (seL4_CPtr)endpoint;
    service_cnode = (seL4_CPtr)cnode;
    service_receive_slot = (seL4_CPtr)receive_slot;
    service_next_export_slot = (seL4_CPtr)first_export_slot;
    service_depth = (seL4_Word)depth;
    service_ready = 1;
    return FACET_OK;
}

FacetResult libfacet_platform_export(void *context,
                                     FacetPlatformDispatch dispatch,
                                     FacetHandle *out_handle)
{
    if (!service_ready || dispatch == NULL || out_handle == NULL ||
        service_export_count == SERVICE_EXPORT_MAX)
        return FACET_INVALID_ARGUMENT;
    uint32_t object_id = (uint32_t)service_export_count + 1;
    seL4_CPtr destination = service_next_export_slot++;
    seL4_Word badge = object_id;
    seL4_Error error = seL4_CNode_Mint(
        service_cnode, destination, service_depth,
        service_cnode, service_endpoint, service_depth,
        seL4_AllRights, badge);
    if (error != seL4_NoError) return FACET_ERROR;
    ServiceHandle *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        (void)seL4_CNode_Delete(service_cnode, destination, service_depth);
        return FACET_OUT_OF_MEMORY;
    }
    service_exports[service_export_count++] = (ServiceExport){
        .context = context,
        .dispatch = dispatch,
        .handle_cap = destination,
    };
    *handle = (ServiceHandle){
        .endpoint = destination,
        .owns_cap = 0,
        .object_id = object_id,
    };
    out_handle->platform = handle;
    return FACET_OK;
}

FacetResult libfacet_platform_unexport(FacetHandle handle)
{
    ServiceHandle *native = as_handle(handle);
    if (native == NULL || native->object_id == 0 ||
        native->object_id > service_export_count)
        return FACET_INVALID_HANDLE;
    ServiceExport *entry = &service_exports[native->object_id - 1];
    entry->dispatch = NULL;
    if (entry->handle_cap != seL4_CapNull) {
        (void)seL4_CNode_Delete(service_cnode, entry->handle_cap, service_depth);
        entry->handle_cap = seL4_CapNull;
    }
    return FACET_OK;
}

static FacetResult decode_request(seL4_MessageInfo_t info,
                                  FacetRpcMessage *request)
{
    size_t length = seL4_MessageInfo_get_length(info);
    if (length < 4) return FACET_PROTOCOL_ERROR;
    request->method_id = (uint32_t)seL4_GetMR(0);
    request->protocol_version = (uint32_t)seL4_GetMR(1);
    request->flags = (uint32_t)seL4_GetMR(2);
    request->payload_size = (size_t)seL4_GetMR(3);
    if (request->protocol_version != FACET_RPC_PROTOCOL_VERSION ||
        request->payload_size > FACET_RPC_MAX_INLINE_PAYLOAD)
        return FACET_PROTOCOL_ERROR;
    size_t payload_word_count = wire_words(request->payload_size);
    if (payload_word_count > length - 4) return FACET_PROTOCOL_ERROR;
    request->word_count = length - 4 - payload_word_count;
    if (request->word_count > FACET_RPC_MAX_WORDS)
        return FACET_PROTOCOL_ERROR;
    for (size_t i = 0; i < request->word_count; i++)
        request->words[i] = seL4_GetMR((int)(4 + i));
    if (request->payload_size != 0) {
        request->payload = malloc(request->payload_size);
        if (request->payload == NULL) return FACET_OUT_OF_MEMORY;
        request->payload_capacity = request->payload_size;
        for (size_t i = 0; i < payload_word_count; i++) {
            seL4_Word word = seL4_GetMR((int)(4 + request->word_count + i));
            size_t offset = i * sizeof(word);
            size_t amount = request->payload_size - offset;
            if (amount > sizeof(word)) amount = sizeof(word);
            memcpy(request->payload + offset, &word, amount);
        }
    }
    size_t extra = seL4_MessageInfo_get_extraCaps(info);
    if (extra > 1) return FACET_PROTOCOL_ERROR;
    if (extra == 1) {
        ServiceHandle *handle = calloc(1, sizeof(*handle));
        if (handle == NULL) return FACET_OUT_OF_MEMORY;
        handle->endpoint = service_receive_slot;
        request->attachments[0].kind = FACET_RPC_ATTACHMENT_HANDLE;
        request->attachments[0].handle.platform = handle;
        request->attachment_count = 1;
    }
    return FACET_OK;
}

static seL4_MessageInfo_t encode_reply(FacetRpcMessage *reply,
                                       FacetResult dispatch_result)
{
    if (reply->word_count == 0) {
        reply->word_count = 1;
        reply->words[0] = (uint64_t)(int64_t)dispatch_result;
    }
    size_t payload_words = wire_words(reply->payload_size);
    if (reply->payload_size > FACET_RPC_MAX_INLINE_PAYLOAD ||
        reply->word_count == 0 || reply->word_count - 1 > FACET_RPC_MAX_WORDS ||
        4 + reply->word_count - 1 + payload_words > seL4_MsgMaxLength ||
        reply->attachment_count > 1) {
        reply->word_count = 1;
        reply->words[0] = (uint64_t)(int64_t)FACET_BUFFER_TOO_SMALL;
        reply->payload_size = 0;
        reply->attachment_count = 0;
        payload_words = 0;
    }
    seL4_SetMR(0, (seL4_Word)reply->words[0]);
    seL4_SetMR(1, FACET_RPC_PROTOCOL_VERSION);
    seL4_SetMR(2, reply->flags);
    seL4_SetMR(3, reply->payload_size);
    for (size_t i = 1; i < reply->word_count; i++)
        seL4_SetMR((int)(3 + i), (seL4_Word)reply->words[i]);
    for (size_t i = 0; i < payload_words; i++) {
        seL4_Word word = 0;
        size_t offset = i * sizeof(word);
        size_t amount = reply->payload_size - offset;
        if (amount > sizeof(word)) amount = sizeof(word);
        memcpy(&word, reply->payload + offset, amount);
        seL4_SetMR((int)(3 + reply->word_count + i), word);
    }
    if (reply->attachment_count == 1) {
        ServiceHandle *handle = as_handle(reply->attachments[0].handle);
        seL4_SetCap(0, handle == NULL ? seL4_CapNull : handle->endpoint);
    }
    return seL4_MessageInfo_new(0, 0, reply->attachment_count,
                                3 + reply->word_count + payload_words);
}

FacetResult facet_sel4_service_run(void)
{
    if (!service_ready) return FACET_INVALID_ARGUMENT;
    for (;;) {
        seL4_SetCapReceivePath(service_cnode, service_receive_slot,
                               service_depth);
        seL4_Word badge = 0;
        seL4_MessageInfo_t info = seL4_Recv(service_endpoint, &badge);
        FacetRpcMessage request = {0};
        FacetRpcMessage reply = {.protocol_version = FACET_RPC_PROTOCOL_VERSION};
        FacetResult result = decode_request(info, &request);
        if (result == FACET_OK) {
            if (badge == 0 || badge > service_export_count ||
                service_exports[badge - 1].dispatch == NULL)
                result = FACET_INVALID_HANDLE;
            else
                result = service_exports[badge - 1].dispatch(
                    service_exports[badge - 1].context, &request, &reply);
        }
        seL4_Reply(encode_reply(&reply, result));
        free(request.payload);
        free(reply.payload);
        if (request.attachment_count != 0) {
            free(request.attachments[0].handle.platform);
            (void)seL4_CNode_Delete(service_cnode, service_receive_slot,
                                    service_depth);
        }
    }
}
