#pragma once

#include <facetos/libfacet/common.h>

static inline const char *facet_filesystem_error(FacetResult result)
{
    switch (result) {
    case FACET_ACCESS_DENIED:
        return "permission denied";
    case FACET_NOT_FOUND:
        return "not found";
    default:
        return "operation failed";
    }
}
