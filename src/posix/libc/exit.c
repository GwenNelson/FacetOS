#include <facet_posix_runtime.h>

#include <facetos/interfaces/IPOSIXView.h>

#include <stdlib.h>

__attribute__((noreturn)) void _exit(int status)
{
    IPOSIXView *view = facet_posix_view();
    if (view != NULL) (void)view->exit_process(view->self, status);
    for (;;) facet_posix_yield();
}

__attribute__((noreturn)) void exit(int status)
{
    _exit(status);
}
