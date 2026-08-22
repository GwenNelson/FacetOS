#pragma once

#include <facetos/libfacet/common.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lightweight client-only entry points; the implementation owns seL4 use.
 * The CNode arguments describe the client's empty capability receive slot. */
FacetResult facet_sel4_client_init(
    uint64_t receive_cnode,
    uint64_t receive_slot,
    uint64_t receive_depth);
FacetResult facet_sel4_client_yield(void);

#ifdef __cplusplus
}
#endif
