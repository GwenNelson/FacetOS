#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IDomainEnvironment.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/ILogger.h>
#include <facetos/interfaces/IPageAllocator.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform/allocator.h"

static FacetResult child_log(ILogger *logger, uint32_t event)
{
    return logger->log(logger->self, 40, event);
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDomainEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ILogger_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK)
        return 1;

    IGenericObject *root;
    if (platform_init(&argc, &argv, &root) != FACET_OK)
        return 1;

    IDomainEnvironment *environment =
        (IDomainEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IDomainEnvironment);
    ILogger *logger = (ILogger *)libfacet_proxy_client_get_interface(root, IID_ILogger);
    IPageAllocator *page_allocator =
        (IPageAllocator *)libfacet_proxy_client_get_interface(root, IID_IPageAllocator);
    if (environment == NULL || logger == NULL || page_allocator == NULL) {
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(page_allocator);
        libfacet_free_proxy_client(root);
        return 1;
    }

    (void)logger->flush(logger->self);
    if (child_log(logger, 1) != FACET_OK) {
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(page_allocator);
        libfacet_free_proxy_client(root);
        return 1;
    }
    if (dominit_allocator_use_pages(page_allocator) != 0) {
        child_log(logger, 4);
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(root);
        return 1;
    }
    child_log(logger, 2);

    /* This is deliberately larger than the bootstrap heap's remaining
     * liballoc region. It proves allocations now reach this domain's
     * IPageAllocator rather than dominit0's own allocator. */
    void *allocation_probe = malloc(512u * 1024u);
    if (allocation_probe == NULL) {
        child_log(logger, 5);
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(root);
        return 1;
    }
    free(allocation_probe);

    child_log(logger, 3);

    libfacet_free_proxy_client(environment);
    libfacet_free_proxy_client(logger);
    libfacet_free_proxy_client(root);

    /* allocator.c retains this proxy for every later liballoc page request. */

    for (;;)
        (void)platform_yield();
}
