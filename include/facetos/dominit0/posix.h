#pragma once

#include <facetos/interfaces/IPOSIXView.h>

#include <facetos/libfacet/common.h>

typedef struct Dominit0PosixView Dominit0PosixView;
typedef FacetResult (*Dominit0PosixSpawn)(void *context, const FacetString *path,
    const FacetArray_string *argv, FacetHandle session, int32_t *pid, int32_t *error);
typedef FacetResult (*Dominit0PosixWait)(void *context, int32_t pid,
    int32_t *status, int32_t *error);
typedef int (*Dominit0PosixCwdChanged)(void *context, FacetHandle directory,
                                       bool synthetic_etc);

Dominit0PosixView *dominit0_posix_view_create(
    FacetHandle stdin_handle, FacetHandle stdout_handle,
    FacetHandle files_handle, FacetHandle cwd_handle);
int dominit0_posix_view_set_root(Dominit0PosixView *view,
                                 FacetHandle root_handle);
int dominit0_posix_view_bind_page_allocator(Dominit0PosixView *view,
                                             FacetHandle allocator);
int dominit0_posix_view_bind_lifecycle(Dominit0PosixView *view,
                                       FacetHandle lifecycle);
int dominit0_posix_view_bind_process_control(Dominit0PosixView *view,
    void *context, uint64_t domain_id, int32_t pid, FacetHandle default_session, Dominit0PosixSpawn spawn,
    Dominit0PosixWait wait);
int dominit0_posix_view_bind_cwd_sync(Dominit0PosixView *view,
    void *context, Dominit0PosixCwdChanged changed, bool synthetic_etc);
void dominit0_posix_view_set_synthetic_cwd(Dominit0PosixView *view,
                                           bool synthetic_etc);
FacetHandle dominit0_posix_view_handle(const Dominit0PosixView *view);
void dominit0_posix_view_destroy(Dominit0PosixView *view);
