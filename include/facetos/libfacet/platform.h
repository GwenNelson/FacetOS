#pragma once

#include <facetos/libfacet/common.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef FacetResult (*FacetPlatformDispatch)(
    void *context,
    const FacetRpcMessage *request,
    FacetRpcMessage *reply
);

FacetResult libfacet_platform_handle_from(
    uint64_t value,
    FacetHandle *out_handle
);

FacetResult libfacet_platform_handle_clone(
    FacetHandle source,
    FacetHandle *destination
);

FacetResult libfacet_platform_handle_release(FacetHandle handle);

FacetResult libfacet_platform_call(
    FacetHandle target,
    const FacetRpcMessage *request,
    FacetRpcMessage *reply
);

FacetResult libfacet_platform_export(
    void *context,
    FacetPlatformDispatch dispatch,
    FacetHandle *out_handle
);

FacetResult libfacet_platform_unexport(FacetHandle handle);

FacetResult libfacet_platform_method_handle(
    FacetHandle object,
    uint32_t method_id,
    FacetHandle *out_handle
);

#ifdef __cplusplus
}
#endif
