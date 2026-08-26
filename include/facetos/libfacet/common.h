#pragma once

#include <facetos/uuid.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACET_RPC_PROTOCOL_VERSION 1u
#define FACET_RPC_MAX_WORDS 64u
#define FACET_RPC_MAX_HANDLES 8u
#define FACET_RPC_MAX_ATTACHMENTS 8u
#define FACET_RPC_MAX_INLINE_PAYLOAD 512u

/* The seL4 transport may impose a smaller attachment limit. */

typedef int32_t FacetResult;

enum {
    FACET_OK = 0,
    FACET_ERROR = -1,
    FACET_INVALID_ARGUMENT = -2,
    FACET_NO_INTERFACE = -3,
    FACET_NO_SUCH_METHOD = -4,
    FACET_ACCESS_DENIED = -5,
    FACET_OUT_OF_MEMORY = -6,
    FACET_INVALID_HANDLE = -7,
    FACET_PROTOCOL_ERROR = -8,
    FACET_BUFFER_TOO_SMALL = -9,
    FACET_NOT_SUPPORTED = -10,
    FACET_NOT_FOUND = -11,
};

typedef struct FacetHandle {
    void *platform;
} FacetHandle;

typedef struct FacetString {
    const char *data;
    size_t length;
} FacetString;

typedef struct FacetArray {
    void *data;
    size_t count;
} FacetArray;

typedef enum FacetType {
    FACET_TYPE_U8,
    FACET_TYPE_U16,
    FACET_TYPE_U32,
    FACET_TYPE_U64,
    FACET_TYPE_I8,
    FACET_TYPE_I16,
    FACET_TYPE_I32,
    FACET_TYPE_I64,
    FACET_TYPE_BOOL,
    FACET_TYPE_UUID,
    FACET_TYPE_HANDLE,
    FACET_TYPE_STRING,
    FACET_TYPE_BYTES,
    FACET_TYPE_LOCAL_PTR,
    FACET_TYPE_ENUM,
    FACET_TYPE_STRUCT,
    FACET_TYPE_ARRAY,
} FacetType;

typedef enum FacetParamDirection {
    FACET_PARAM_IN,
    FACET_PARAM_OUT,
    FACET_PARAM_INOUT,
} FacetParamDirection;

enum {
    FACET_PARAM_NULLABLE = 1u << 0,
    FACET_PARAM_OWNED = 1u << 1,
};

typedef struct FacetParamMeta {
    const char *name;
    FacetType type;
    FacetParamDirection direction;
    uint32_t flags;
    int32_t length_parameter;
    const struct FacetTypeMeta *type_metadata;
} FacetParamMeta;

typedef struct FacetEnumValueMeta {
    const char *name;
    int64_t value;
} FacetEnumValueMeta;

typedef struct FacetStructFieldMeta {
    const char *name;
    FacetType type_kind;
    const struct FacetTypeMeta *type;
    size_t offset;
} FacetStructFieldMeta;

typedef struct FacetTypeMeta {
    FacetType kind;
    FacetType underlying_kind;
    const char *name;
    size_t size;
    const struct FacetTypeMeta *element_type;
    FacetType element_kind;
    size_t fixed_length;
    const FacetEnumValueMeta *enum_values;
    size_t enum_value_count;
    const FacetStructFieldMeta *struct_fields;
    size_t struct_field_count;
} FacetTypeMeta;

typedef struct FacetRpcCodec {
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t offset;
} FacetRpcCodec;

FacetResult facet_rpc_codec_init(FacetRpcCodec *codec, size_t capacity);
void facet_rpc_codec_destroy(FacetRpcCodec *codec);
FacetResult facet_rpc_encode_value(
    FacetRpcCodec *codec,
    FacetType type,
    const FacetTypeMeta *metadata,
    const void *value
);
FacetResult facet_rpc_decode_value(
    FacetRpcCodec *codec,
    FacetType type,
    const FacetTypeMeta *metadata,
    void *value
);
void facet_rpc_release_value(
    FacetType type,
    const FacetTypeMeta *metadata,
    void *value
);

typedef enum FacetRpcAttachmentKind {
    FACET_RPC_ATTACHMENT_HANDLE,
    FACET_RPC_ATTACHMENT_BUFFER,
} FacetRpcAttachmentKind;

typedef struct FacetRpcAttachment {
    FacetRpcAttachmentKind kind;
    FacetHandle handle;
    size_t size;
} FacetRpcAttachment;

typedef struct FacetRpcMessage {
    uint32_t protocol_version;
    uint32_t method_id;
    uint32_t flags;
    size_t word_count;
    uint64_t words[FACET_RPC_MAX_WORDS];
    /* Legacy fields retained for source compatibility; attachments are used
     * by the current transport. */
    size_t handle_count;
    FacetHandle handles[FACET_RPC_MAX_HANDLES];
    uint8_t *payload;
    size_t payload_size;
    size_t payload_capacity;
    size_t attachment_count;
    FacetRpcAttachment attachments[FACET_RPC_MAX_ATTACHMENTS];
} FacetRpcMessage;

typedef struct FacetMethodMeta FacetMethodMeta;
typedef struct FacetInterfaceMeta FacetInterfaceMeta;

typedef void (*FacetProxyInitializer)(void *interface_object, void *state);
typedef FacetResult (*FacetServerMethod)(
    void *interface_object,
    const FacetRpcMessage *request,
    FacetRpcMessage *reply
);

struct FacetMethodMeta {
    uint32_t method_id;
    const char *name;
    size_t function_offset;
    size_t parameter_count;
    const FacetParamMeta *parameters;
    FacetServerMethod server_method;
};

struct FacetInterfaceMeta {
    uuid_t iid;
    const char *name;
    size_t interface_size;
    size_t required_interface_count;
    const uuid_t *required_interfaces;
    size_t method_count;
    const FacetMethodMeta *methods;
    FacetProxyInitializer initialize_proxy;
};

/* Generated IGenericObject headers provide the complete definition. */
typedef struct IGenericObject IGenericObject;

void *libfacet_new_proxy_client(
    const FacetInterfaceMeta *metadata,
    FacetHandle handle
);

void libfacet_free_proxy_client(void *interface_object);

FacetResult libfacet_proxy_client_call(
    void *self,
    const FacetMethodMeta *method,
    ...
);

void *libfacet_proxy_client_get_interface(void *self, uuid_t iid);

FacetResult libfacet_export_interface(
    void *interface_object,
    const FacetInterfaceMeta *metadata,
    FacetHandle *out_handle
);

FacetResult libfacet_unexport_interface(FacetHandle handle);
FacetResult libfacet_handle_clone(FacetHandle source, FacetHandle *destination);
FacetResult libfacet_handle_release(FacetHandle handle);
FacetResult libfacet_get_method_handle(
    void *interface_object,
    const FacetMethodMeta *method,
    FacetHandle *out_handle
);

FacetResult libfacet_register_generic_metadata(
    const FacetInterfaceMeta *metadata
);
FacetResult libfacet_register_interface_metadata(
    const FacetInterfaceMeta *metadata
);

IGenericObject *libfacet_proxy_from(uint64_t platform_handle);

/* Takes ownership of handle and constructs a client proxy for metadata. */
void *libfacet_proxy_from_handle(const FacetInterfaceMeta *metadata,
                                 FacetHandle handle);

#ifdef __cplusplus
}
#endif
