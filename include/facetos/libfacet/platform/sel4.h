#pragma once

#include <facetos/libfacet/platform.h>

#include <sel4/sel4.h>
#include <simple-default/simple-default.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FacetSel4PlatformConfig {
    vka_t *vka;
    vspace_t *vspace;
    simple_t *simple;
} FacetSel4PlatformConfig;

FacetResult facet_sel4_platform_init(
    const FacetSel4PlatformConfig *config
);

FacetResult facet_sel4_handle_get_cap(
    FacetHandle handle,
    seL4_CPtr *out_cap
);

FacetResult facet_sel4_handle_from_cap(
    seL4_CPtr cap,
    FacetHandle *out_handle
);

#ifdef __cplusplus
}
#endif
