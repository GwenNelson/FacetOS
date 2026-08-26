#pragma once

#include <facetos/libfacet/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the selected platform backend and return the process's initial
 * root object.  Platform-private arguments are removed from argc/argv only
 * after successful initialisation. */
FacetResult platform_init(
    int *argc,
    char ***argv,
    IGenericObject **out_root);

/* Yield execution according to the selected platform's scheduling model. */
FacetResult platform_yield(void);

/* Enter the domain bootstrap service loop after it has exported its local
 * domain-owned interfaces. This does not return on a healthy seL4 runtime. */
FacetResult platform_run_services(void);

#ifdef __cplusplus
}
#endif
