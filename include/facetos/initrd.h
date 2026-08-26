#pragma once

#include <facetos/libfacet/common.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FacetInitrd FacetInitrd;
typedef struct InitrdEntry FacetInitrdNode;

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

/* Trusted loader-side execute check. Directory components require search
 * permission; the target must carry an execute bit. */
FacetResult facet_initrd_check_execute(FacetInitrd *initrd, const char *path,
                                      uint32_t uid, uint32_t gid, bool admin);

/* Server-side primitives used by credential-filtered views. These never make
 * RPC calls and never expose the opaque node pointer to a client. */
FacetResult facet_initrd_open_node(FacetInitrd *initrd, const char *base,
                                  const FacetString *path, bool directory,
                                  FacetInitrdNode **node);
const char *facet_initrd_node_path(const FacetInitrdNode *node);
FacetResult facet_initrd_node_metadata(const FacetInitrdNode *node,
                                      uint32_t *mode, uint32_t *uid,
                                      uint32_t *gid);
FacetResult facet_initrd_node_metadata_handle(FacetInitrdNode *node,
                                             FacetHandle *handle);
FacetResult facet_initrd_node_size(const FacetInitrdNode *node,
                                  uint64_t *size);
FacetResult facet_initrd_node_read(const FacetInitrdNode *node,
                                  uint64_t offset, uint32_t maximum,
                                  FacetArray_u8 *data);
FacetResult facet_initrd_node_list(const FacetInitrdNode *node,
                                  uint64_t cursor, uint32_t maximum,
                                  FacetArray_Entry *entries,
                                  uint64_t *next_cursor, bool *end);
