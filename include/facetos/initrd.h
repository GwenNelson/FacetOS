#pragma once

#include <facetos/libfacet/common.h>

#include <stddef.h>
#include <stdint.h>

typedef struct FacetInitrd FacetInitrd;

/* Parses a read-only newc CPIO image.  The input storage must outlive the
 * returned object.  Paths exposed by the resulting store are absolute. */
FacetInitrd *facet_initrd_create(const void *data, size_t size);
void facet_initrd_destroy(FacetInitrd *initrd);

/* Exporting is deliberately separate from parsing, keeping CPIO validation
 * independent of the selected RPC platform. */
FacetResult facet_initrd_export(FacetInitrd *initrd, FacetHandle *store);
