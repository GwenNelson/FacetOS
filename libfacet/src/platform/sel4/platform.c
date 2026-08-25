#include <facetos/libfacet/platform/sel4.h>

#include <sel4utils/thread.h>
#include <sel4utils/thread_config.h>
#include <vka/capops.h>
#include <vka/ipcbuffer.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct FacetSel4Handle FacetSel4Handle;
typedef struct FacetSel4Export FacetSel4Export;

#define FACET_SEL4_FLAG_SHARED_PAYLOAD (1u << 31)
#define FACET_SEL4_BUFFER_COUNT_SHIFT 24
#define FACET_SEL4_BUFFER_COUNT_MASK 0x7fu
/* Each exported server reserves receive slots in dominit0's bootstrap CSpace.
 * Current methods transfer either one object or one bulk frame, never both;
 * reserve exactly the capability count the implemented ABI requires. */
#define FACET_SEL4_MAX_ATTACHMENTS 1u
/* Facet RPC dispatch is deliberately small and non-recursive.  A full
 * sel4utils 64 KiB server stack for every exported interface prematurely
 * exhausts dominit0 while it is setting up per-domain environments. */
#define FACET_SEL4_SERVER_STACK_PAGES 4u

typedef struct FacetSel4BulkState {
    vka_object_t frames[FACET_RPC_MAX_ATTACHMENTS];
    void *mapped[FACET_RPC_MAX_ATTACHMENTS];
    size_t count;
} FacetSel4BulkState;

struct FacetSel4Handle {
    seL4_CPtr endpoint;
    uint32_t method_id;
    FacetSel4Export *export_state;
    int owns_cap;
};

struct FacetSel4Export {
    vka_object_t endpoint;
    sel4utils_thread_t thread;
    void *context;
    FacetPlatformDispatch dispatch;
    cspacepath_t receive_paths[FACET_RPC_MAX_ATTACHMENTS];
    size_t receive_path_count;
};

static FacetSel4PlatformConfig platform_config;
static int platform_ready;

#ifdef DEBUG
static void export_trace(const char *message)
{
    seL4_DebugPutString((char *)message);
}
#else
static void export_trace(const char *message)
{
    (void)message;
}
#endif

static size_t wire_word_count(size_t bytes)
{
    if (bytes > SIZE_MAX - (sizeof(seL4_Word) - 1)) return SIZE_MAX;
    return (bytes + sizeof(seL4_Word) - 1) / sizeof(seL4_Word);
}

static void free_receive_paths(cspacepath_t *paths, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        vka_cnode_delete(&paths[i]);
        vka_cspace_free_path(platform_config.vka, paths[i]);
    }
}

static void bulk_cleanup(FacetSel4BulkState *bulk)
{
    if (bulk == NULL) return;
    for (size_t i = 0; i < bulk->count; i++) {
        if (bulk->mapped[i] != NULL)
            vspace_unmap_pages(platform_config.vspace, bulk->mapped[i],
                               1, seL4_PageBits, VSPACE_PRESERVE);
        vka_free_object(platform_config.vka, &bulk->frames[i]);
    }
    memset(bulk, 0, sizeof(*bulk));
}

static void release_reply_handles(FacetRpcMessage *reply)
{
    if (reply == NULL) return;
    for (size_t i = 0; i < reply->attachment_count; i++) {
        if (reply->attachments[i].kind == FACET_RPC_ATTACHMENT_HANDLE &&
            reply->attachments[i].handle.platform != NULL) {
            FacetSel4Handle *handle =
                (FacetSel4Handle *)reply->attachments[i].handle.platform;
            if (handle->owns_cap)
                libfacet_platform_handle_release(reply->attachments[i].handle);
            reply->attachments[i].handle.platform = NULL;
        }
    }
}

static FacetResult bulk_prepare(FacetRpcMessage *message,
                                FacetSel4BulkState *bulk,
                                FacetSel4Handle *handles)
{
    if (message->payload_size != 0 && message->payload == NULL)
        return FACET_INVALID_ARGUMENT;
    if (message->payload_size <= FACET_RPC_MAX_INLINE_PAYLOAD) return FACET_OK;
    size_t page_size = BIT(seL4_PageBits);
    size_t pages = message->payload_size > SIZE_MAX - page_size + 1
        ? SIZE_MAX : (message->payload_size + page_size - 1) / page_size;
    if (pages == SIZE_MAX || pages > FACET_SEL4_MAX_ATTACHMENTS ||
        message->attachment_count > FACET_SEL4_MAX_ATTACHMENTS - pages)
        return FACET_BUFFER_TOO_SMALL;
    message->flags |= FACET_SEL4_FLAG_SHARED_PAYLOAD;
    message->flags &= ~(FACET_SEL4_BUFFER_COUNT_MASK << FACET_SEL4_BUFFER_COUNT_SHIFT);
    message->flags |= (uint32_t)(pages << FACET_SEL4_BUFFER_COUNT_SHIFT);
    const uint8_t *source = message->payload;
    for (size_t i = 0; i < pages; i++) {
        if (vka_alloc_frame(platform_config.vka, seL4_PageBits,
                            &bulk->frames[i]) != 0) {
            bulk->count = i;
            bulk_cleanup(bulk);
            return FACET_OUT_OF_MEMORY;
        }
        bulk->mapped[i] = vspace_map_pages(
            platform_config.vspace, &bulk->frames[i].cptr, NULL,
            seL4_AllRights, 1, seL4_PageBits, 1);
        if (bulk->mapped[i] == NULL) {
            bulk->count = i + 1;
            bulk_cleanup(bulk);
            return FACET_ERROR;
        }
        size_t offset = i * page_size;
        size_t copy = message->payload_size - offset;
        if (copy > page_size) copy = page_size;
        memcpy(bulk->mapped[i], source + offset, copy);
        handles[i].endpoint = bulk->frames[i].cptr;
        handles[i].method_id = 0;
        handles[i].export_state = NULL;
        handles[i].owns_cap = 0;
        message->attachments[message->attachment_count + i].kind =
            FACET_RPC_ATTACHMENT_BUFFER;
        message->attachments[message->attachment_count + i].handle.platform =
            &handles[i];
        message->attachments[message->attachment_count + i].size = copy;
        bulk->count = i + 1;
    }
    message->attachment_count += pages;
    return FACET_OK;
}

static FacetResult bulk_read_received(FacetRpcMessage *message,
                                       size_t first_buffer,
                                       size_t buffer_count,
                                       const seL4_CPtr *caps)
{
    if ((message->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) == 0)
        return FACET_OK;
    if (buffer_count == 0 || message->payload_size == 0 ||
        buffer_count > FACET_SEL4_MAX_ATTACHMENTS ||
        first_buffer + buffer_count > message->attachment_count)
        return FACET_PROTOCOL_ERROR;
    size_t page_size = BIT(seL4_PageBits);
    if (buffer_count > SIZE_MAX / page_size ||
        message->payload_size > buffer_count * page_size)
        return FACET_PROTOCOL_ERROR;
    message->payload = malloc(message->payload_size);
    if (message->payload == NULL) return FACET_OUT_OF_MEMORY;
    message->payload_capacity = message->payload_size;
    size_t offset = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        seL4_CPtr cap = caps[i];
        void *mapped = vspace_map_pages(platform_config.vspace, &cap, NULL,
                                        seL4_AllRights, 1, seL4_PageBits, 1);
        if (mapped == NULL) return FACET_ERROR;
        size_t copy = message->payload_size - offset;
        if (copy > page_size) copy = page_size;
        memcpy(message->payload + offset, mapped, copy);
        vspace_unmap_pages(platform_config.vspace, mapped, 1,
                           seL4_PageBits, VSPACE_PRESERVE);
        offset += copy;
    }
    return offset == message->payload_size ? FACET_OK : FACET_PROTOCOL_ERROR;
}

static int alloc_receive_paths(cspacepath_t *paths, size_t count)
{
    size_t allocated = 0;
    for (; allocated < count; allocated++) {
        if (vka_cspace_alloc_path(platform_config.vka, &paths[allocated]) != 0)
            break;
        if (allocated != 0 &&
            (paths[allocated].root != paths[0].root ||
             paths[allocated].capDepth != paths[0].capDepth ||
             paths[allocated].capPtr + allocated != paths[0].capPtr)) {
            vka_cspace_free_path(platform_config.vka, paths[allocated]);
            break;
        }
    }
    if (allocated != count) {
        free_receive_paths(paths, allocated);
        return -1;
    }
    return 0;
}

static FacetSel4Handle *as_handle(FacetHandle handle)
{
    return (FacetSel4Handle *)handle.platform;
}

static FacetResult message_from_request(
    const FacetRpcMessage *request,
    seL4_MessageInfo_t *info)
{
    size_t payload_words = request == NULL ? 0 :
        ((request->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) != 0
            ? 0 : wire_word_count(request->payload_size));
    if (request == NULL || info == NULL ||
        request->word_count > FACET_RPC_MAX_WORDS ||
        payload_words == SIZE_MAX ||
        request->word_count > SIZE_MAX - payload_words - 3 ||
        request->word_count + payload_words + 3 > seL4_MsgMaxLength ||
        request->attachment_count > FACET_SEL4_MAX_ATTACHMENTS ||
        (request->payload_size != 0 && request->payload == NULL)) {
        return FACET_INVALID_ARGUMENT;
    }

    seL4_SetMR(0, request->method_id);
    seL4_SetMR(1, request->protocol_version);
    seL4_SetMR(2, request->flags);
    seL4_SetMR(3, request->payload_size);
    for (size_t i = 0; i < request->word_count; i++) {
        seL4_SetMR(i + 4, (seL4_Word)request->words[i]);
    }
    for (size_t i = 0; i < payload_words; i++) {
        seL4_Word word = 0;
        size_t offset = i * sizeof(word);
        size_t copy = request->payload_size - offset;
        if (copy > sizeof(word)) copy = sizeof(word);
        memcpy(&word, request->payload + offset, copy);
        seL4_SetMR(request->word_count + 4 + i, word);
    }
    for (size_t i = 0; i < request->attachment_count; i++) {
        seL4_CPtr cap = seL4_CapNull;
        if ((request->attachments[i].kind == FACET_RPC_ATTACHMENT_HANDLE ||
             request->attachments[i].kind == FACET_RPC_ATTACHMENT_BUFFER) &&
            request->attachments[i].handle.platform != NULL) {
            cap = ((FacetSel4Handle *)request->attachments[i].handle.platform)->endpoint;
        }
        seL4_SetCap((int)i, cap);
    }
    *info = seL4_MessageInfo_new(
        0, 0, (seL4_Word)request->attachment_count,
        (seL4_Word)(request->word_count + payload_words + 4));
    return FACET_OK;
}

static FacetResult request_from_message(
    seL4_MessageInfo_t info,
    FacetRpcMessage *request)
{
    request->protocol_version = FACET_RPC_PROTOCOL_VERSION;
    request->method_id = seL4_GetMR(0);
    request->protocol_version = seL4_GetMR(1);
    request->flags = seL4_GetMR(2);
    request->payload_size = seL4_GetMR(3);
    if (request->protocol_version != FACET_RPC_PROTOCOL_VERSION)
        return FACET_PROTOCOL_ERROR;

    seL4_Word length = seL4_MessageInfo_get_length(info);
    size_t payload_words = (request->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) != 0
        ? 0 : wire_word_count(request->payload_size);
    if (payload_words == SIZE_MAX) {
        request->word_count = 0;
        request->payload_size = 0;
        return FACET_PROTOCOL_ERROR;
    }
    size_t available = length >= 4 ? length - 4 : 0;
    if (available < payload_words || payload_words > seL4_MsgMaxLength - 4)
        return FACET_PROTOCOL_ERROR;
    uint8_t inline_payload[seL4_MsgMaxLength * sizeof(seL4_Word)];
    if ((request->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) == 0 &&
        request->payload_size > sizeof(inline_payload))
        return FACET_PROTOCOL_ERROR;
    request->word_count = available >= payload_words
        ? available - payload_words : 0;
    if (request->word_count > FACET_RPC_MAX_WORDS)
        return FACET_PROTOCOL_ERROR;
    for (size_t i = 0; i < request->word_count; i++)
        request->words[i] = seL4_GetMR(4 + i);
    if ((request->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) == 0) {
        for (size_t i = 0; i < payload_words; i++) {
            seL4_Word word = seL4_GetMR(4 + request->word_count + i);
            size_t offset = i * sizeof(word);
            size_t copy = request->payload_size - offset;
            if (copy > sizeof(word)) copy = sizeof(word);
            memcpy(inline_payload + offset, &word, copy);
        }
    }
    if ((request->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) == 0 &&
        request->payload_size != 0 && payload_words <= available) {
        request->payload = malloc(request->payload_size);
        request->payload_capacity = request->payload_size;
        if (request->payload == NULL) return FACET_OUT_OF_MEMORY;
        memcpy(request->payload, inline_payload, request->payload_size);
    }
    return FACET_OK;
}

static seL4_MessageInfo_t reply_to_message(const FacetRpcMessage *reply)
{
    size_t payload_words = wire_word_count(reply->payload_size);
    size_t length = reply->word_count + payload_words + 3;
    if (payload_words == SIZE_MAX || length < reply->word_count ||
        length > seL4_MsgMaxLength ||
        (reply->payload_size != 0 && reply->payload == NULL)) {
        seL4_SetMR(0, (seL4_Word)(uint32_t)FACET_BUFFER_TOO_SMALL);
        seL4_SetMR(1, FACET_RPC_PROTOCOL_VERSION);
        seL4_SetMR(2, 0);
        seL4_SetMR(3, 0);
        return seL4_MessageInfo_new(0, 0, 0, 4);
    }
    if (reply->attachment_count > FACET_SEL4_MAX_ATTACHMENTS) {
        seL4_SetMR(0, (seL4_Word)(uint32_t)FACET_BUFFER_TOO_SMALL);
        seL4_SetMR(1, FACET_RPC_PROTOCOL_VERSION);
        seL4_SetMR(2, 0);
        seL4_SetMR(3, 0);
        return seL4_MessageInfo_new(0, 0, 0, 4);
    }
    seL4_SetMR(0, reply->word_count == 0 ? 0 : (seL4_Word)reply->words[0]);
    seL4_SetMR(1, FACET_RPC_PROTOCOL_VERSION);
    seL4_SetMR(2, reply->flags);
    seL4_SetMR(3, reply->payload_size);
    for (size_t i = 1; i < reply->word_count && i + 3 < length; i++) {
        seL4_SetMR(i + 3, (seL4_Word)reply->words[i]);
    }
    for (size_t i = 0; i < payload_words && i + reply->word_count + 3 < length; i++) {
        seL4_Word word = 0;
        size_t offset = i * sizeof(word);
        size_t copy = reply->payload_size - offset;
        if (copy > sizeof(word)) copy = sizeof(word);
        memcpy(&word, reply->payload + offset, copy);
        seL4_SetMR(reply->word_count + 3 + i, word);
    }
    for (size_t i = 0; i < reply->attachment_count; i++) {
        FacetSel4Handle *handle = (FacetSel4Handle *)reply->attachments[i].handle.platform;
        seL4_SetCap((int)i, handle == NULL ? seL4_CapNull : handle->endpoint);
    }
    return seL4_MessageInfo_new(0, 0, (seL4_Word)reply->attachment_count,
                                (seL4_Word)length);
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
        vka_set_cap_receive_path(&export_state->receive_paths[0]);
        seL4_MessageInfo_t received =
            seL4_Recv(export_state->endpoint.cptr, &badge);
        FacetRpcMessage request = {0};
        FacetRpcMessage reply = {0};
        FacetResult request_result = request_from_message(received, &request);
        size_t extra_caps = seL4_MessageInfo_get_extraCaps(received);
        if (extra_caps > FACET_SEL4_MAX_ATTACHMENTS)
            extra_caps = FACET_SEL4_MAX_ATTACHMENTS;
        size_t buffer_count = (request.flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) != 0
            ? (request.flags >> FACET_SEL4_BUFFER_COUNT_SHIFT) & FACET_SEL4_BUFFER_COUNT_MASK : 0;
        if (buffer_count > extra_caps) request_result = FACET_PROTOCOL_ERROR;
        seL4_CPtr received_caps[FACET_RPC_MAX_ATTACHMENTS] = {0};
        for (size_t i = 0; i < extra_caps; i++) {
            /* Transferred caps are installed at the configured receive path.
             * caps_or_badges contains sender-side IPC metadata, not a stable
             * destination CPtr in this CSpace. */
            received_caps[i] = export_state->receive_paths[i].capPtr;
            request.attachments[i].kind = i >= extra_caps - buffer_count
                ? FACET_RPC_ATTACHMENT_BUFFER : FACET_RPC_ATTACHMENT_HANDLE;
            request.attachment_count++;
            if (request.attachments[i].kind == FACET_RPC_ATTACHMENT_HANDLE &&
                facet_sel4_handle_from_cap(received_caps[i],
                                           &request.attachments[i].handle) != FACET_OK)
                request.attachments[i].kind = FACET_RPC_ATTACHMENT_BUFFER;
        }
        if (request_result == FACET_OK && buffer_count != 0)
            request_result = bulk_read_received(&request,
                                                extra_caps - buffer_count,
                                                buffer_count,
                                                &received_caps[extra_caps - buffer_count]);
        reply.protocol_version = FACET_RPC_PROTOCOL_VERSION;
        reply.word_count = 0;

        if (request_result != FACET_OK) {
            reply.words[reply.word_count++] = (uint64_t)(int64_t)request_result;
            seL4_Reply(reply_to_message(&reply));
            free(request.payload);
            for (size_t i = 0; i < request.attachment_count; i++) {
                free(request.attachments[i].handle.platform);
                vka_cnode_delete(&export_state->receive_paths[i]);
            }
            continue;
        }

        FacetResult result = export_state->dispatch(
            export_state->context, &request, &reply);
        if (reply.word_count == 0) {
            reply.words[0] = (uint64_t)(int64_t)result;
            reply.word_count = 1;
        }
        FacetSel4BulkState reply_bulk = {0};
        FacetSel4Handle reply_bulk_handles[FACET_RPC_MAX_ATTACHMENTS] = {0};
        if (bulk_prepare(&reply, &reply_bulk, reply_bulk_handles) != FACET_OK) {
            free(reply.payload);
            reply.payload = NULL;
            reply.payload_size = 0;
            reply.payload_capacity = 0;
            reply.attachment_count = 0;
            reply.word_count = 1;
            reply.words[0] = (uint64_t)(int64_t)FACET_BUFFER_TOO_SMALL;
        }
        seL4_Reply(reply_to_message(&reply));
        bulk_cleanup(&reply_bulk);
        free(request.payload);
        free(reply.payload);
        release_reply_handles(&reply);
        for (size_t i = 0; i < request.attachment_count; i++) {
            free(request.attachments[i].handle.platform);
            cspacepath_t received_path = export_state->receive_paths[i];
            vka_cnode_delete(&received_path);
        }
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
    native->owns_cap = 0;
    out_handle->platform = native;
    return FACET_OK;
}

static FacetResult stable_handle_from_received(
    seL4_CPtr received_cap, FacetHandle *out_handle)
{
    cspacepath_t source;
    cspacepath_t destination;
    vka_cspace_make_path(platform_config.vka, received_cap, &source);
    if (vka_cspace_alloc_path(platform_config.vka, &destination) != 0)
        return FACET_OUT_OF_MEMORY;
    if (vka_cnode_copy(&destination, &source, seL4_AllRights) != 0) {
        vka_cspace_free_path(platform_config.vka, destination);
        return FACET_ERROR;
    }
    FacetResult result = facet_sel4_handle_from_cap(destination.capPtr,
                                                    out_handle);
    if (result != FACET_OK) {
        vka_cnode_delete(&destination);
        vka_cspace_free_path(platform_config.vka, destination);
        return result;
    }
    ((FacetSel4Handle *)out_handle->platform)->owns_cap = 1;
    return FACET_OK;
}

static void release_message_handles(FacetRpcMessage *message)
{
    if (message == NULL) return;
    for (size_t i = 0; i < message->attachment_count; i++) {
        if (message->attachments[i].kind == FACET_RPC_ATTACHMENT_HANDLE &&
            message->attachments[i].handle.platform != NULL)
            libfacet_platform_handle_release(message->attachments[i].handle);
        message->attachments[i].handle.platform = NULL;
    }
    message->attachment_count = 0;
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
    cspacepath_t source_path;
    cspacepath_t destination_path;
    vka_cspace_make_path(platform_config.vka, native->endpoint, &source_path);
    if (vka_cspace_alloc_path(platform_config.vka, &destination_path) != 0) {
        free(copy);
        return FACET_OUT_OF_MEMORY;
    }
    if (vka_cnode_copy(&destination_path, &source_path, seL4_AllRights) != 0) {
        vka_cspace_free_path(platform_config.vka, destination_path);
        free(copy);
        return FACET_ERROR;
    }
    copy->endpoint = destination_path.capPtr;
    copy->method_id = native->method_id;
    copy->export_state = native->export_state;
    copy->owns_cap = 1;
    destination->platform = copy;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_release(FacetHandle handle)
{
    FacetSel4Handle *native = as_handle(handle);
    if (native == NULL) {
        return FACET_INVALID_HANDLE;
    }
    if (native->owns_cap) {
        cspacepath_t path;
        vka_cspace_make_path(platform_config.vka, native->endpoint, &path);
        vka_cnode_delete(&path);
        vka_cspace_free_path(platform_config.vka, path);
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

    FacetSel4BulkState request_bulk = {0};
    FacetSel4Handle request_bulk_handles[FACET_RPC_MAX_ATTACHMENTS] = {0};
    FacetResult bulk_result = bulk_prepare(&actual, &request_bulk,
                                           request_bulk_handles);
    if (bulk_result != FACET_OK) return bulk_result;
    seL4_MessageInfo_t info;
    FacetResult result = message_from_request(&actual, &info);
    if (result != FACET_OK) {
        bulk_cleanup(&request_bulk);
        return result;
    }
    cspacepath_t receive_paths[FACET_RPC_MAX_ATTACHMENTS];
    if (alloc_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS) != 0) {
        bulk_cleanup(&request_bulk);
        return FACET_OUT_OF_MEMORY;
    }
    vka_set_cap_receive_path(&receive_paths[0]);
    seL4_MessageInfo_t received = seL4_Call(native->endpoint, info);
    bulk_cleanup(&request_bulk);
    reply->word_count = 0;
    reply->protocol_version = seL4_GetMR(1);
    reply->flags = seL4_GetMR(2);
    reply->payload_size = seL4_GetMR(3);
    if (reply->protocol_version != FACET_RPC_PROTOCOL_VERSION) {
        free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
        return FACET_PROTOCOL_ERROR;
    }
    seL4_Word length = seL4_MessageInfo_get_length(received);
    size_t payload_words = (reply->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) != 0
        ? 0 : wire_word_count(reply->payload_size);
    if (payload_words == SIZE_MAX) {
        free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
        return FACET_PROTOCOL_ERROR;
    }
    size_t available = length >= 4 ? length - 4 : 0;
    reply->word_count = 1 + (available >= payload_words
                             ? available - payload_words : 0);
    if (reply->word_count > FACET_RPC_MAX_WORDS) {
        free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
        return FACET_PROTOCOL_ERROR;
    }
    reply->words[0] = seL4_GetMR(0);
    for (size_t i = 1; i < reply->word_count; i++)
        reply->words[i] = seL4_GetMR(i + 3);
    if ((reply->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) == 0 &&
        reply->payload_size != 0 && payload_words <= available) {
        reply->payload = malloc(reply->payload_size);
        reply->payload_capacity = reply->payload_size;
        if (reply->payload == NULL) {
            free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
            return FACET_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < payload_words; i++) {
            seL4_Word word = seL4_GetMR(4 + reply->word_count - 1 + i);
            size_t offset = i * sizeof(word);
            size_t copy = reply->payload_size - offset;
            if (copy > sizeof(word)) copy = sizeof(word);
            memcpy(reply->payload + offset, &word, copy);
        }
    }
    size_t extra_caps = seL4_MessageInfo_get_extraCaps(received);
    if (extra_caps > FACET_SEL4_MAX_ATTACHMENTS)
        extra_caps = FACET_SEL4_MAX_ATTACHMENTS;
    size_t buffer_count = (reply->flags & FACET_SEL4_FLAG_SHARED_PAYLOAD) != 0
        ? (reply->flags >> FACET_SEL4_BUFFER_COUNT_SHIFT) & FACET_SEL4_BUFFER_COUNT_MASK : 0;
    if (buffer_count > extra_caps) {
        free(reply->payload);
        free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
        return FACET_PROTOCOL_ERROR;
    }
    seL4_CPtr received_caps[FACET_RPC_MAX_ATTACHMENTS] = {0};
    for (size_t i = 0; i < extra_caps; i++) {
        received_caps[i] = receive_paths[i].capPtr;
        reply->attachments[i].kind = i >= extra_caps - buffer_count
            ? FACET_RPC_ATTACHMENT_BUFFER : FACET_RPC_ATTACHMENT_HANDLE;
        if (reply->attachments[i].kind == FACET_RPC_ATTACHMENT_HANDLE &&
            stable_handle_from_received(received_caps[i],
                                         &reply->attachments[i].handle) != FACET_OK) {
            free(reply->payload);
            release_message_handles(reply);
            free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
            return FACET_OUT_OF_MEMORY;
        }
        reply->attachment_count++;
    }
    if (buffer_count != 0) {
        FacetResult read_result = bulk_read_received(
            reply, extra_caps - buffer_count, buffer_count,
            &received_caps[extra_caps - buffer_count]);
        if (read_result != FACET_OK) {
            free(reply->payload);
            release_message_handles(reply);
            free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
            return read_result;
        }
    }
    free_receive_paths(receive_paths, FACET_SEL4_MAX_ATTACHMENTS);
    return FACET_OK;
}

FacetResult libfacet_platform_export(
    void *context,
    FacetPlatformDispatch dispatch,
    FacetHandle *out_handle)
{
    export_trace("\nlibfacet export: enter\n");
    if (!platform_ready || context == NULL || dispatch == NULL ||
        out_handle == NULL) {
        export_trace("libfacet export: invalid arguments\n");
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
        export_trace("libfacet export: endpoint allocation failed\n");
        free(export_state);
        free(handle);
        return FACET_ERROR;
    }
    export_trace("libfacet export: endpoint allocated\n");

    if (alloc_receive_paths(export_state->receive_paths,
                            FACET_SEL4_MAX_ATTACHMENTS) != 0) {
            export_trace("libfacet export: receive paths failed\n");
            vka_free_object(platform_config.vka, &export_state->endpoint);
            free(export_state);
            free(handle);
            return FACET_OUT_OF_MEMORY;
    }
    export_state->receive_path_count = FACET_SEL4_MAX_ATTACHMENTS;
    export_trace("libfacet export: receive paths allocated\n");

    seL4_Word cspace_data = api_make_guard_skip_word(
        seL4_WordBits - simple_get_cnode_size_bits(platform_config.simple));
    sel4utils_thread_config_t thread_config = thread_config_default(
        platform_config.simple,
        simple_get_cnode(platform_config.simple),
        cspace_data,
        seL4_CapNull,
        seL4_MaxPrio);
    thread_config = thread_config_stack_size(thread_config,
                                             FACET_SEL4_SERVER_STACK_PAGES);
    error = sel4utils_configure_thread_config(
        platform_config.vka, platform_config.vspace, platform_config.vspace,
        thread_config, &export_state->thread);
    if (error != 0) {
        export_trace("libfacet export: thread configuration failed\n");
        free_receive_paths(export_state->receive_paths,
                           export_state->receive_path_count);
        vka_free_object(platform_config.vka, &export_state->endpoint);
        free(export_state);
        free(handle);
        return FACET_ERROR;
    }
    export_trace("libfacet export: thread configured\n");

    export_state->context = context;
    export_state->dispatch = dispatch;
    error = sel4utils_start_thread(
        &export_state->thread,
        facet_sel4_server_entry,
        export_state, NULL, 1);
    if (error != 0) {
        export_trace("libfacet export: thread start failed\n");
        sel4utils_clean_up_thread(platform_config.vka,
                                  platform_config.vspace,
                                  &export_state->thread);
        free_receive_paths(export_state->receive_paths,
                           export_state->receive_path_count);
        vka_free_object(platform_config.vka, &export_state->endpoint);
        free(export_state);
        free(handle);
        return FACET_ERROR;
    }
    export_trace("libfacet export: thread started\n");

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
    free_receive_paths(export_state->receive_paths,
                       export_state->receive_path_count);
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
