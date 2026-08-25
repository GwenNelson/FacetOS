#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IDomainEnvironment.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/ILogger.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform/allocator.h"

static FacetResult child_log(ILogger *logger, const char *text)
{
    FacetString message = { .data = text, .length = 0 };
    while (text[message.length] != '\0')
        message.length++;
    return logger->log(logger->self, 40, &message);
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDomainEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ILogger_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK)
        return 1;

    IGenericObject *root;
    if (platform_init(&argc, &argv, &root) != FACET_OK)
        return 1;

    IDomainEnvironment *environment =
        (IDomainEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IDomainEnvironment);
    IProcessEnvironment *process_environment =
        (IProcessEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IProcessEnvironment);
    FacetString logger_name = { .data = "logger", .length = 6 };
    FacetString allocator_name = { .data = "memory.pages", .length = 12 };
    FacetHandle logger_handle = {0}, allocator_handle = {0};
    ILogger *logger = NULL;
    IPageAllocator *page_allocator = NULL;
    if (environment != NULL && process_environment != NULL &&
        process_environment->resolve(process_environment->self, &logger_name,
                                     &logger_handle) == FACET_OK &&
        process_environment->resolve(process_environment->self, &allocator_name,
                                     &allocator_handle) == FACET_OK) {
        logger = libfacet_proxy_from_handle(&ILogger_MetaData, logger_handle);
        page_allocator = libfacet_proxy_from_handle(&IPageAllocator_MetaData,
                                                    allocator_handle);
    }
    if (environment == NULL || process_environment == NULL || logger == NULL ||
        page_allocator == NULL) {
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(process_environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(page_allocator);
        libfacet_free_proxy_client(root);
        return 1;
    }

    (void)logger->flush(logger->self);
    if (child_log(logger, "received environment IPageAllocator") != FACET_OK) {
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(process_environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(page_allocator);
        libfacet_free_proxy_client(root);
        return 1;
    }
    if (dominit_allocator_use_pages(page_allocator) != 0) {
        child_log(logger, "could not activate IPageAllocator");
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(process_environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(root);
        return 1;
    }
    child_log(logger, "activated IPageAllocator");

    /* This is deliberately larger than the bootstrap heap's remaining
     * liballoc region. It proves allocations now reach this domain's
     * IPageAllocator rather than dominit0's own allocator. */
    void *allocation_probe = malloc(512u * 1024u);
    if (allocation_probe == NULL) {
        child_log(logger, "could not allocate through IPageAllocator");
        libfacet_free_proxy_client(environment);
        libfacet_free_proxy_client(process_environment);
        libfacet_free_proxy_client(logger);
        libfacet_free_proxy_client(root);
        return 1;
    }
    free(allocation_probe);

    child_log(logger, "IPageAllocator/liballoc ready");

    libfacet_free_proxy_client(environment);
    libfacet_free_proxy_client(process_environment);
    libfacet_free_proxy_client(logger);
    libfacet_free_proxy_client(root);

    /* allocator.c retains this proxy for every later liballoc page request. */

    for (;;)
        (void)platform_yield();
}
