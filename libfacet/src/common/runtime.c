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

static size_t codec_type_size(FacetType type, const FacetTypeMeta *metadata)
{
    if (type == FACET_TYPE_ENUM && metadata != NULL) {
        type = metadata->underlying_kind;
    }
    switch (type) {
    case FACET_TYPE_U8: case FACET_TYPE_I8: return 1;
    case FACET_TYPE_U16: case FACET_TYPE_I16: return 2;
    case FACET_TYPE_U32: case FACET_TYPE_I32: return 4;
    case FACET_TYPE_U64: case FACET_TYPE_I64: return 8;
    case FACET_TYPE_BOOL: return 1;
    case FACET_TYPE_UUID: return sizeof(uuid_t);
    case FACET_TYPE_STRING: return sizeof(FacetString);
    case FACET_TYPE_ARRAY: return sizeof(FacetArray);
    case FACET_TYPE_STRUCT: return metadata == NULL ? 0 : metadata->size;
    default: return 0;
    }
}

static void codec_release_value(FacetType type, const FacetTypeMeta *metadata,
                                void *value);

FacetResult facet_rpc_codec_init(FacetRpcCodec *codec, size_t capacity)
{
    if (codec == NULL) return FACET_INVALID_ARGUMENT;
    memset(codec, 0, sizeof(*codec));
    codec->capacity = capacity == 0 ? 256 : capacity;
    codec->data = malloc(codec->capacity);
    return codec->data == NULL ? FACET_OUT_OF_MEMORY : FACET_OK;
}

void facet_rpc_codec_destroy(FacetRpcCodec *codec)
{
    if (codec == NULL) return;
    free(codec->data);
    memset(codec, 0, sizeof(*codec));
}

static FacetResult codec_write(FacetRpcCodec *codec, const void *data, size_t size)
{
    if (codec == NULL || (size != 0 && data == NULL)) return FACET_INVALID_ARGUMENT;
    if (size > SIZE_MAX - codec->size) return FACET_BUFFER_TOO_SMALL;
    if (codec->size + size > codec->capacity) {
        size_t capacity = codec->capacity == 0 ? 256 : codec->capacity;
        while (capacity < codec->size + size) {
            if (capacity > SIZE_MAX / 2) return FACET_BUFFER_TOO_SMALL;
            capacity *= 2;
        }
        void *expanded = realloc(codec->data, capacity);
        if (expanded == NULL) return FACET_OUT_OF_MEMORY;
        codec->data = expanded;
        codec->capacity = capacity;
    }
    memcpy(codec->data + codec->size, data, size);
    codec->size += size;
    return FACET_OK;
}

static FacetResult codec_read(FacetRpcCodec *codec, void *data, size_t size);

static FacetResult codec_write_uint(FacetRpcCodec *codec, uint64_t value,
                                    size_t size)
{
    uint8_t bytes[8];
    if (size > sizeof(bytes)) return FACET_INVALID_ARGUMENT;
    for (size_t i = 0; i < size; i++) bytes[i] = (uint8_t)(value >> (i * 8));
    return codec_write(codec, bytes, size);
}

static FacetResult codec_read_uint(FacetRpcCodec *codec, uint64_t *value,
                                   size_t size)
{
    uint8_t bytes[8];
    if (value == NULL || size > sizeof(bytes)) return FACET_INVALID_ARGUMENT;
    FacetResult result = codec_read(codec, bytes, size);
    if (result != FACET_OK) return result;
    *value = 0;
    for (size_t i = 0; i < size; i++) *value |= (uint64_t)bytes[i] << (i * 8);
    return FACET_OK;
}

static FacetResult codec_read(FacetRpcCodec *codec, void *data, size_t size)
{
    if (codec == NULL || data == NULL || codec->offset > codec->size ||
        size > codec->size - codec->offset) return FACET_PROTOCOL_ERROR;
    memcpy(data, codec->data + codec->offset, size);
    codec->offset += size;
    return FACET_OK;
}

static FacetResult codec_value(
    FacetRpcCodec *codec,
    FacetType type,
    const FacetTypeMeta *metadata,
    const void *input,
    void *output,
    int decoding)
{
    if (type == FACET_TYPE_ENUM && metadata != NULL) {
        type = metadata->underlying_kind;
    }
    if (type == FACET_TYPE_STRING) {
        if (decoding) {
            uint64_t decoded_length = 0;
            FacetResult result = codec_read_uint(codec, &decoded_length, 4);
            uint32_t length = (uint32_t)decoded_length;
            if (result != FACET_OK || codec->offset > codec->size ||
                length > codec->size - codec->offset) {
                return FACET_PROTOCOL_ERROR;
            }
            FacetString *string = output;
            string->data = malloc((size_t)length + 1);
            if (string->data == NULL) return FACET_OUT_OF_MEMORY;
            result = codec_read(codec, (void *)string->data, length);
            if (result != FACET_OK) {
                free((void *)string->data);
                string->data = NULL;
                return result;
            }
            ((char *)string->data)[length] = '\0';
            string->length = length;
            return FACET_OK;
        }
        const FacetString *string = input;
        if (string == NULL || string->length > UINT32_MAX) return FACET_INVALID_ARGUMENT;
        uint32_t length = (uint32_t)string->length;
        FacetResult result = codec_write_uint(codec, length, 4);
        return result == FACET_OK ? codec_write(codec, string->data, string->length) : result;
    }
    if (type == FACET_TYPE_ARRAY) {
        const FacetTypeMeta *element = metadata;
        FacetType element_kind = element == NULL ? FACET_TYPE_BYTES : element->element_kind;
        if (element != NULL && element->element_type != NULL) element_kind = element->element_type->kind;
        if (decoding) {
            uint64_t decoded_count = 0;
            FacetResult result = codec_read_uint(codec, &decoded_count, 4);
            uint32_t count = (uint32_t)decoded_count;
            if (result != FACET_OK) return result;
            FacetArray *array = output;
            size_t element_size = codec_type_size(element_kind,
                                                   element == NULL ? NULL : element->element_type);
            if (element_size == 0 && count != 0) return FACET_PROTOCOL_ERROR;
            if (count != 0 && count > SIZE_MAX / element_size) return FACET_PROTOCOL_ERROR;
            if (count == 0) {
                array->data = NULL;
                array->count = 0;
                return FACET_OK;
            }
            array->data = calloc(count, element_size);
            if (array->data == NULL && count != 0) return FACET_OUT_OF_MEMORY;
            array->count = count;
            for (size_t i = 0; i < count; i++) {
                result = codec_value(codec, element_kind,
                                     element == NULL ? NULL : element->element_type,
                                     NULL, (char *)array->data + i * element_size, 1);
                if (result != FACET_OK) {
                    codec_release_value(element_kind,
                                        element == NULL ? NULL : element->element_type,
                                        array->data);
                    free(array->data);
                    array->data = NULL;
                    array->count = 0;
                    return result;
                }
            }
            return FACET_OK;
        }
        const FacetArray *array = input;
        if (array == NULL || array->count > UINT32_MAX) return FACET_INVALID_ARGUMENT;
        uint32_t count = (uint32_t)array->count;
        FacetResult result = codec_write_uint(codec, count, 4);
        size_t element_size = codec_type_size(element_kind,
                                               element == NULL ? NULL : element->element_type);
        if (element_size == 0 && array->count != 0) return FACET_NOT_SUPPORTED;
        if (array->count != 0 && array->data == NULL) return FACET_INVALID_ARGUMENT;
        for (size_t i = 0; result == FACET_OK && i < array->count; i++) {
            result = codec_value(codec, element_kind,
                                 element == NULL ? NULL : element->element_type,
                                 (const char *)array->data + i * element_size, NULL, 0);
        }
        return result;
    }
    if (type == FACET_TYPE_STRUCT) {
        if (metadata == NULL) return FACET_INVALID_ARGUMENT;
        for (size_t i = 0; i < metadata->struct_field_count; i++) {
            const FacetStructFieldMeta *field = &metadata->struct_fields[i];
            const char *base = decoding ? (char *)output : (const char *)input;
            FacetResult result = codec_value(codec, field->type_kind, field->type,
                decoding ? NULL : base + field->offset,
                decoding ? (char *)output + field->offset : NULL, decoding);
            if (result != FACET_OK) return result;
        }
        return FACET_OK;
    }
    if (type == FACET_TYPE_HANDLE || type == FACET_TYPE_LOCAL_PTR ||
        type == FACET_TYPE_BYTES) return FACET_NOT_SUPPORTED;

    size_t size = codec_type_size(type, metadata);
    if (size == 0) return FACET_NOT_SUPPORTED;
    if (type == FACET_TYPE_UUID) {
        return decoding ? codec_read(codec, output, sizeof(uuid_t))
                        : codec_write(codec, input, sizeof(uuid_t));
    }
    if (decoding) {
        uint64_t value = 0;
        FacetResult result = codec_read_uint(codec, &value, size);
        if (result != FACET_OK) return result;
        switch (type) {
        case FACET_TYPE_U8: *(uint8_t *)output = (uint8_t)value; break;
        case FACET_TYPE_U16: *(uint16_t *)output = (uint16_t)value; break;
        case FACET_TYPE_U32: *(uint32_t *)output = (uint32_t)value; break;
        case FACET_TYPE_U64: *(uint64_t *)output = value; break;
        case FACET_TYPE_I8: *(int8_t *)output = (int8_t)value; break;
        case FACET_TYPE_I16: *(int16_t *)output = (int16_t)value; break;
        case FACET_TYPE_I32: *(int32_t *)output = (int32_t)value; break;
        case FACET_TYPE_I64: *(int64_t *)output = (int64_t)value; break;
        case FACET_TYPE_BOOL: *(bool *)output = value != 0; break;
        default: return FACET_NOT_SUPPORTED;
        }
        return FACET_OK;
    }
    uint64_t value;
    switch (type) {
    case FACET_TYPE_U8: value = *(const uint8_t *)input; break;
    case FACET_TYPE_U16: value = *(const uint16_t *)input; break;
    case FACET_TYPE_U32: value = *(const uint32_t *)input; break;
    case FACET_TYPE_U64: value = *(const uint64_t *)input; break;
    case FACET_TYPE_I8: value = (uint64_t)(int64_t)*(const int8_t *)input; break;
    case FACET_TYPE_I16: value = (uint64_t)(int64_t)*(const int16_t *)input; break;
    case FACET_TYPE_I32: value = (uint64_t)(int64_t)*(const int32_t *)input; break;
    case FACET_TYPE_I64: value = (uint64_t)*(const int64_t *)input; break;
    case FACET_TYPE_BOOL: value = *(const int *)input != 0; break;
    default: return FACET_NOT_SUPPORTED;
    }
    return codec_write_uint(codec, value, size);
}

static void codec_release_value(FacetType type, const FacetTypeMeta *metadata,
                                void *value)
{
    if (value == NULL) return;
    if (type == FACET_TYPE_STRING) {
        FacetString *string = value;
        free((void *)string->data);
        string->data = NULL;
        string->length = 0;
    } else if (type == FACET_TYPE_ARRAY) {
        FacetArray *array = value;
        FacetType element_kind = metadata == NULL ? FACET_TYPE_BYTES : metadata->element_kind;
        const FacetTypeMeta *element = metadata == NULL ? NULL : metadata->element_type;
        size_t element_size = codec_type_size(element_kind, element);
        if (array->data != NULL && element_size != 0 &&
            (element_kind == FACET_TYPE_STRING || element_kind == FACET_TYPE_ARRAY ||
             element_kind == FACET_TYPE_STRUCT)) {
            for (size_t i = 0; i < array->count; i++)
                codec_release_value(element_kind, element,
                                    (char *)array->data + i * element_size);
        }
        free(array->data);
        array->data = NULL;
        array->count = 0;
    } else if (type == FACET_TYPE_STRUCT && metadata != NULL) {
        for (size_t i = 0; i < metadata->struct_field_count; i++) {
            const FacetStructFieldMeta *field = &metadata->struct_fields[i];
            if (field->type_kind == FACET_TYPE_STRING ||
                field->type_kind == FACET_TYPE_ARRAY ||
                field->type_kind == FACET_TYPE_STRUCT)
                codec_release_value(field->type_kind, field->type,
                                    (char *)value + field->offset);
        }
    }
}

void facet_rpc_release_value(FacetType type, const FacetTypeMeta *metadata,
                             void *value)
{
    codec_release_value(type, metadata, value);
}

FacetResult facet_rpc_encode_value(
    FacetRpcCodec *codec, FacetType type,
    const FacetTypeMeta *metadata, const void *value)
{
    return codec_value(codec, type, metadata, value, NULL, 0);
}

FacetResult facet_rpc_decode_value(
    FacetRpcCodec *codec, FacetType type,
    const FacetTypeMeta *metadata, void *value)
{
    return codec_value(codec, type, metadata, NULL, value, 1);
}

static const FacetInterfaceMeta *generic_metadata;
#define FACET_MAX_REGISTERED_INTERFACES 64u
static const FacetInterfaceMeta *registered_interfaces[FACET_MAX_REGISTERED_INTERFACES];
static size_t registered_interface_count;

static int uuid_matches(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

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

    FacetHandle handle = state->handle;
    state->magic = 0;
    libfacet_handle_release(handle);
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

static int append_input_value(FacetRpcMessage *message,
                              const FacetParamMeta *parameter,
                              const void *value)
{
    if (parameter->type == FACET_TYPE_HANDLE) {
        if (message->attachment_count >= FACET_RPC_MAX_ATTACHMENTS) return -1;
        message->attachments[message->attachment_count].kind = FACET_RPC_ATTACHMENT_HANDLE;
        message->attachments[message->attachment_count++].handle = *(const FacetHandle *)value;
        return 0;
    }
    if (parameter->type != FACET_TYPE_STRING &&
        parameter->type != FACET_TYPE_ARRAY &&
        parameter->type != FACET_TYPE_STRUCT) {
        switch (parameter->type) {
        case FACET_TYPE_U8: return append_word(message, *(const uint8_t *)value);
        case FACET_TYPE_U16: return append_word(message, *(const uint16_t *)value);
        case FACET_TYPE_U32: return append_word(message, *(const uint32_t *)value);
        case FACET_TYPE_BOOL: return append_word(message, *(const bool *)value != 0);
        case FACET_TYPE_ENUM: return append_word(message, *(const int32_t *)value);
        case FACET_TYPE_U64: return append_word(message, *(const uint64_t *)value);
        case FACET_TYPE_I8: return append_word(message, (uint64_t)(int64_t)*(const int8_t *)value);
        case FACET_TYPE_I16: return append_word(message, (uint64_t)(int64_t)*(const int16_t *)value);
        case FACET_TYPE_I32: return append_word(message, (uint64_t)(int64_t)*(const int32_t *)value);
        case FACET_TYPE_I64: return append_word(message, (uint64_t)*(const int64_t *)value);
        case FACET_TYPE_UUID: {
            const uuid_t *uuid = value;
            uint64_t words[2] = {0, 0};
            memcpy(words, uuid->bytes, sizeof(uuid->bytes));
            return append_word(message, words[0]) == 0
                ? append_word(message, words[1]) : -1;
        }
        case FACET_TYPE_LOCAL_PTR:
            return append_word(message, (uint64_t)(uintptr_t)*(void * const *)value);
        default: return -1;
        }
    }
    FacetRpcCodec codec = {
        .data = message->payload,
        .size = message->payload_size,
        .capacity = message->payload_capacity,
    };
    if (codec.data == NULL && facet_rpc_codec_init(&codec, 256) != FACET_OK)
        return -1;
    if (facet_rpc_encode_value(&codec, parameter->type,
                               parameter->type_metadata, value) != FACET_OK)
        return -1;
    message->payload = codec.data;
    message->payload_size = codec.size;
    message->payload_capacity = codec.capacity;
    return 0;
}

static int append_input(
    va_list *arguments,
    FacetRpcMessage *message,
    const FacetParamMeta *parameter)
{
    if (parameter->type == FACET_TYPE_STRING ||
        parameter->type == FACET_TYPE_ARRAY ||
        parameter->type == FACET_TYPE_STRUCT) {
        const void *value = va_arg(*arguments, const void *);
        FacetRpcCodec codec = {
            .data = message->payload,
            .size = message->payload_size,
            .capacity = message->payload_capacity,
        };
        if (codec.data == NULL && facet_rpc_codec_init(&codec, 256) != FACET_OK)
            return -1;
    if (facet_rpc_encode_value(&codec, parameter->type,
                               parameter->type_metadata, value) != FACET_OK)
            return -1;
        message->payload = codec.data;
        message->payload_size = codec.size;
        message->payload_capacity = codec.capacity;
        return 0;
    }
    if (parameter->type == FACET_TYPE_HANDLE) {
        if (message->attachment_count >= FACET_RPC_MAX_ATTACHMENTS) return -1;
        FacetHandle handle = va_arg(*arguments, FacetHandle);
        message->attachments[message->attachment_count].kind = FACET_RPC_ATTACHMENT_HANDLE;
        message->attachments[message->attachment_count++].handle = handle;
        return 0;
    }
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
    case FACET_TYPE_ENUM:
        return append_word(message, (uint64_t)(unsigned int)va_arg(*arguments, int));
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
    size_t *reply_handle_index,
    FacetRpcCodec *payload_codec)
{
    if (destination == NULL) {
        return -1;
    }

    if (parameter->type == FACET_TYPE_HANDLE) {
        if (*reply_handle_index >= reply->attachment_count) return -1;
        if (reply->attachments[*reply_handle_index].kind != FACET_RPC_ATTACHMENT_HANDLE)
            return -1;
        *(FacetHandle *)destination = reply->attachments[(*reply_handle_index)++].handle;
        return 0;
    }
    if (parameter->type == FACET_TYPE_STRING ||
        parameter->type == FACET_TYPE_ARRAY ||
        parameter->type == FACET_TYPE_STRUCT) {
        (void)reply_index;
        (void)reply_handle_index;
        return payload_codec != NULL &&
            facet_rpc_decode_value(payload_codec, parameter->type,
                                   parameter->type_metadata, destination) == FACET_OK
            ? 0 : -1;
    }
    if (parameter->type == FACET_TYPE_UUID) {
        if (*reply_index > reply->word_count ||
            reply->word_count - *reply_index < 2)
            return -1;
        uint64_t words[2] = { reply->words[(*reply_index)++],
                              reply->words[(*reply_index)++] };
        memcpy(((uuid_t *)destination)->bytes, words, sizeof(words));
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
    case FACET_TYPE_ENUM:
        *(int32_t *)destination = (int32_t)word;
        return 0;
    case FACET_TYPE_BOOL:
        *(bool *)destination = word != 0;
        return 0;
    case FACET_TYPE_LOCAL_PTR:
        *(void **)destination = (void *)(uintptr_t)word;
        return 0;
    default:
        return -1;
    }
}

static void release_decoded_outputs(const FacetMethodMeta *method,
                                    void **outputs, size_t output_count)
{
    size_t output_index = 0;
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetParamMeta *parameter = &method->parameters[i];
        if (parameter->direction == FACET_PARAM_IN) continue;
        if (output_index >= output_count) break;
        if (parameter->type == FACET_TYPE_STRING ||
            parameter->type == FACET_TYPE_ARRAY ||
            parameter->type == FACET_TYPE_STRUCT)
            facet_rpc_release_value(parameter->type,
                                    parameter->type_metadata,
                                    outputs[output_index]);
        output_index++;
    }
}

static void release_reply_attachments(FacetRpcMessage *reply)
{
    if (reply == NULL) return;
    for (size_t i = 0; i < reply->attachment_count; i++) {
        if (reply->attachments[i].kind == FACET_RPC_ATTACHMENT_HANDLE &&
            reply->attachments[i].handle.platform != NULL)
            libfacet_handle_release(reply->attachments[i].handle);
        reply->attachments[i].handle.platform = NULL;
    }
    reply->attachment_count = 0;
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
            void *output = va_arg(arguments, void *);
            outputs[output_count++] = output;
            if (parameter->direction == FACET_PARAM_INOUT) {
                if (append_input_value(&request, parameter, output) != 0) {
                    va_end(arguments);
                    free(request.payload);
                    return FACET_NOT_SUPPORTED;
                }
            }
        } else if (append_input(&arguments, &request, parameter) != 0) {
            va_end(arguments);
            free(request.payload);
            return FACET_NOT_SUPPORTED;
        }
    }
    va_end(arguments);

    FacetRpcMessage reply = {0};
    FacetResult result = libfacet_platform_call(
        state->handle, &request, &reply);
    if (result != FACET_OK) {
        free(request.payload);
        return result;
    }
    if (reply.word_count == 0) {
        free(request.payload);
        free(reply.payload);
        release_reply_attachments(&reply);
        return FACET_PROTOCOL_ERROR;
    }

    result = (FacetResult)(int32_t)reply.words[0];
    if (result != FACET_OK) {
        free(request.payload);
        free(reply.payload);
        release_reply_attachments(&reply);
        return result;
    }

    size_t reply_index = 1;
    size_t reply_handle_index = 0;
    FacetRpcCodec reply_codec = {
        .data = reply.payload,
        .size = reply.payload_size,
        .capacity = reply.payload_size,
    };
    size_t output_index = 0;
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetParamMeta *parameter = &method->parameters[i];
        if (parameter->direction == FACET_PARAM_IN) {
            continue;
        }
        if (assign_output(parameter, outputs[output_index++],
                          &reply, &reply_index, &reply_handle_index,
                          &reply_codec) != 0) {
            release_decoded_outputs(method, outputs, output_index - 1);
            free(request.payload);
            free(reply.payload);
            release_reply_attachments(&reply);
            return FACET_PROTOCOL_ERROR;
        }
    }
    free(request.payload);
    free(reply.payload);
    return FACET_OK;
}

void *libfacet_proxy_client_get_interface(void *self, uuid_t iid)
{
    if (self == NULL || generic_metadata == NULL) return NULL;
    const FacetMethodMeta *get_interface = find_method(generic_metadata, 0);
    if (get_interface == NULL) return NULL;
    FacetHandle handle = {0};
    if (libfacet_proxy_client_call(self, get_interface, iid, &handle) != FACET_OK)
        return NULL;
    const FacetInterfaceMeta *metadata = NULL;
    for (size_t i = 0; i < registered_interface_count; i++) {
        if (uuid_matches(registered_interfaces[i]->iid, iid)) {
            metadata = registered_interfaces[i];
            break;
        }
    }
    if (metadata == NULL) {
        libfacet_handle_release(handle);
        return NULL;
    }
    void *proxy = libfacet_new_proxy_client(metadata, handle);
    if (proxy == NULL) libfacet_handle_release(handle);
    return proxy;
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
    return libfacet_register_interface_metadata(metadata);
}

FacetResult libfacet_register_interface_metadata(
    const FacetInterfaceMeta *metadata)
{
    if (metadata == NULL) return FACET_INVALID_ARGUMENT;
    for (size_t i = 0; i < registered_interface_count; i++) {
        if (uuid_matches(registered_interfaces[i]->iid, metadata->iid)) {
            registered_interfaces[i] = metadata;
            return FACET_OK;
        }
    }
    if (registered_interface_count >= FACET_MAX_REGISTERED_INTERFACES)
        return FACET_BUFFER_TOO_SMALL;
    registered_interfaces[registered_interface_count++] = metadata;
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

void *libfacet_proxy_from_handle(const FacetInterfaceMeta *metadata,
                                 FacetHandle handle)
{
    if (metadata == NULL || handle.platform == NULL)
        return NULL;
    void *proxy = libfacet_new_proxy_client(metadata, handle);
    if (proxy == NULL)
        (void)libfacet_handle_release(handle);
    return proxy;
}
