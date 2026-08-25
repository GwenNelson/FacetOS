#pragma once

#include <facetos/libfacet/common.h>

#include <stddef.h>
#include <stdint.h>

typedef struct FacetInitrd FacetInitrd;

/* Parses a read-only newc CPIO image.  The input storage must outlive the
 * returned object.  Directory objects expose canonical absolute paths; store
 * operations resolve relative paths from root. */
FacetInitrd *facet_initrd_create(const void *data, size_t size);
void facet_initrd_destroy(FacetInitrd *initrd);

/* Exporting is deliberately separate from parsing, keeping CPIO validation
 * independent of the selected RPC platform. */
FacetResult facet_initrd_export(FacetInitrd *initrd, FacetHandle *store);

/* Trusted loader-side lookup.  The returned bytes remain owned by initrd. */
FacetResult facet_initrd_find_file(FacetInitrd *initrd, const char *path,
                                  const uint8_t **data, size_t *size);
