#pragma once

#include <facetos/libfacet/common.h>

typedef struct Dominit0PosixView Dominit0PosixView;

Dominit0PosixView *dominit0_posix_view_create(
    FacetHandle stdin_handle, FacetHandle stdout_handle,
    FacetHandle files_handle, FacetHandle cwd_handle);
int dominit0_posix_view_bind_page_allocator(Dominit0PosixView *view,
                                             FacetHandle allocator);
int dominit0_posix_view_bind_lifecycle(Dominit0PosixView *view,
                                       FacetHandle lifecycle);
FacetHandle dominit0_posix_view_handle(const Dominit0PosixView *view);
void dominit0_posix_view_destroy(Dominit0PosixView *view);
