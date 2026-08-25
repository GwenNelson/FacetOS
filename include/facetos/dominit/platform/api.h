#pragma once

#include <facetos/libfacet/common.h>
#include <facetos/interfaces/IPageAllocator.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the selected platform backend and return the process's initial
 * root object.  Platform-private arguments are removed from argc/argv only
 * after successful initialisation. */
FacetResult platform_init(
    int *argc,
    char ***argv,
    IGenericObject **out_root,
    IPageAllocator **out_page_allocator);

/* Yield execution according to the selected platform's scheduling model. */
FacetResult platform_yield(void);

#ifdef __cplusplus
}
#endif
