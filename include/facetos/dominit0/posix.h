#pragma once

#include <facetos/libfacet/common.h>

typedef struct Dominit0PosixView Dominit0PosixView;

Dominit0PosixView *dominit0_posix_view_create(FacetHandle stdout_handle);
FacetHandle dominit0_posix_view_handle(const Dominit0PosixView *view);
void dominit0_posix_view_destroy(Dominit0PosixView *view);
