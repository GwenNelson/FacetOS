#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <facetos/libfacet/common.h>

static const uuid_t IID_IGenericObject = UUID_INIT(0xb8713abf,0x0c5b,0x4f2d,0x87be,0x90e9494ba2b0ULL);
static const char IGenericObject_InterfaceName[] = "IGenericObject";

enum {
    IGenericObject_METHOD_getInterface = 0
};

static const size_t IGenericObject_RequiredInterfacesCount = 0;

typedef struct IGenericObject {
    void *self;
    void *priv;
    FacetResult (*getInterface)(void *self, uuid_t iid, FacetHandle *result);
} IGenericObject;

static const FacetParamMeta IGenericObject_getInterface_Params[] = {
    { "iid", FACET_TYPE_UUID, FACET_PARAM_IN, 0, -1 },
    { "result", FACET_TYPE_HANDLE, FACET_PARAM_OUT, 0, -1 },
};
static const FacetMethodMeta IGenericObject_Methods[];

static inline FacetResult IGenericObject_server_getInterface(void *, const FacetRpcMessage *, FacetRpcMessage *);
static const FacetMethodMeta IGenericObject_Methods[] = {
    { 0, "getInterface", offsetof(IGenericObject, getInterface), 2, IGenericObject_getInterface_Params, IGenericObject_server_getInterface },
};

static inline FacetResult IGenericObject_proxy_getInterface(void *self, uuid_t iid, FacetHandle *result)
{
    return libfacet_proxy_client_call(
        self, &IGenericObject_Methods[0], iid, result);
}

static inline void IGenericObject_initialize_proxy(
    void *interface_object, void *state)
{
    IGenericObject *object = interface_object;
    object->self = object;
    object->priv = state;
    object->getInterface = IGenericObject_proxy_getInterface;
}

static inline FacetResult IGenericObject_server_getInterface(
    void *interface_object,
    const FacetRpcMessage *request,
    FacetRpcMessage *reply)
{
    if (request->word_count != 2) return FACET_PROTOCOL_ERROR;
    uuid_t iid;
    memcpy(&iid, &request->words[0], sizeof(iid));
    FacetHandle result = {0};
    FacetResult call_result = ((IGenericObject *)interface_object)->getInterface(
        ((IGenericObject *)interface_object)->self, iid, &result);
    reply->word_count = 0;
    reply->words[reply->word_count++] = (uint64_t)(int64_t)call_result;
    if (reply->handle_count >= FACET_RPC_MAX_HANDLES) return FACET_BUFFER_TOO_SMALL;
    reply->handles[reply->handle_count++] = result;
    return FACET_OK;
}

static const FacetInterfaceMeta IGenericObject_MetaData = {
    .iid = IID_IGenericObject,
    .name = IGenericObject_InterfaceName,
    .interface_size = sizeof(IGenericObject),
    .required_interface_count = IGenericObject_RequiredInterfacesCount,
    .required_interfaces = NULL,
    .method_count = sizeof(IGenericObject_Methods) / sizeof(IGenericObject_Methods[0]),
    .methods = IGenericObject_Methods,
    .initialize_proxy = IGenericObject_initialize_proxy,
};
