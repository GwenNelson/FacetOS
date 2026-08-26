#include <facetos/libc/native.h>
#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IProcessLifecycle.h>

#include <stdlib.h>

/* Seat servers share libc-native but deliberately use a narrower platform
 * backend. Their terminal state never returns through exit(); retain a
 * freestanding fallback without pulling seL4 into the libc archive. */
extern FacetResult platform_yield(void) __attribute__((weak));

/* Seat servers share the native CRT but have their own bootstrap contract. */
__attribute__((weak)) IProcessEnvironment *facet_native_environment(void)
{
    return NULL;
}

__attribute__((noreturn)) void _exit(int status)
{
    IProcessEnvironment *environment = facet_native_environment();
    if (environment != NULL) {
        FacetString name = {
            .data = "process.lifecycle",
            .length = sizeof("process.lifecycle") - 1,
        };
        FacetHandle handle = {0};
        if (environment->resolve(environment->self, &name, &handle) ==
            FACET_OK) {
            IProcessLifecycle *lifecycle = libfacet_proxy_from_handle(
                &IProcessLifecycle_MetaData, handle);
            if (lifecycle != NULL) {
                (void)lifecycle->notify_exit(lifecycle->self, status);
                libfacet_free_proxy_client(lifecycle);
            }
        }
    }
    for (;;) {
        if (platform_yield != NULL) (void)platform_yield();
        else __asm__ volatile("pause");
    }
}

__attribute__((noreturn)) void exit(int status)
{
    _exit(status);
}
