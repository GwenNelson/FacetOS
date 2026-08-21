#pragma once

#include <facetos/uuid.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACET_RPC_PROTOCOL_VERSION 1u
#define FACET_RPC_MAX_WORDS 64u
#define FACET_RPC_MAX_HANDLES 8u

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
};

typedef struct FacetHandle {
    void *platform;
} FacetHandle;

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
} FacetParamMeta;

typedef struct FacetRpcMessage {
    uint32_t protocol_version;
    uint32_t method_id;
    uint32_t flags;
    size_t word_count;
    uint64_t words[FACET_RPC_MAX_WORDS];
    size_t handle_count;
    FacetHandle handles[FACET_RPC_MAX_HANDLES];
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

IGenericObject *libfacet_proxy_from(uint64_t platform_handle);

#ifdef __cplusplus
}
#endif
