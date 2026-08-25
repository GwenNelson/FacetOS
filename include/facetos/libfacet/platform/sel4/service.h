#pragma once

#include <facetos/libfacet/common.h>

#include <stdint.h>

#include <sel4/sel4.h>

FacetResult facet_sel4_service_init(uint64_t endpoint, uint64_t cnode,
                                    uint64_t receive_slot,
                                    uint64_t first_export_slot,
                                    uint64_t depth);
FacetResult facet_sel4_service_run(void);
FacetResult facet_sel4_service_handle_cap(FacetHandle handle,
                                          seL4_CPtr *out_cap);
