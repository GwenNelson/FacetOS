#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IDomainEnvironment.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/ILogger.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IDomainConfig.h>
#include <facetos/interfaces/IAuthService.h>
#include <facetos/interfaces/IAuthenticatedPrincipal.h>
#include <facetos/interfaces/ISecurityManager.h>
#include <facetos/interfaces/ISession.h>
#include <facetos/interfaces/IByteWriter.h>

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
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDomainConfig_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IAuthService_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IAuthenticatedPrincipal_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISecurityManager_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISession_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK)
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

    FacetHandle domain_config_handle = {0};
    IDomainConfig *domain_config = NULL;
    uint64_t configured_domain_id = UINT64_MAX;
    if (environment->getdomain_config(environment->self, &domain_config_handle) == FACET_OK)
        domain_config = libfacet_proxy_from_handle(&IDomainConfig_MetaData,
                                                   domain_config_handle);
    if (domain_config == NULL ||
        domain_config->getdomain_id(domain_config->self, &configured_domain_id) != FACET_OK) {
        child_log(logger, "could not resolve IDomainConfig");
    } else if (configured_domain_id != 0) {
        child_log(logger, "received configured domain capability");
    }
    libfacet_free_proxy_client(domain_config);

    FacetString auth_name = { .data = "auth", .length = 4 };
    FacetString security_name = { .data = "security", .length = 8 };
    FacetHandle auth_handle = {0}, security_handle = {0}, authenticated = {0}, session = {0};
    IAuthService *auth_service = NULL;
    ISecurityManager *security_manager = NULL;
    if (process_environment->resolve(process_environment->self, &auth_name, &auth_handle) == FACET_OK &&
        process_environment->resolve(process_environment->self, &security_name, &security_handle) == FACET_OK) {
        auth_service = libfacet_proxy_from_handle(&IAuthService_MetaData, auth_handle);
        security_manager = libfacet_proxy_from_handle(&ISecurityManager_MetaData, security_handle);
    }
    if (auth_service != NULL && security_manager != NULL) {
        FacetString root = { .data = "root", .length = 4 };
        FacetString password = { .data = "facetos", .length = 7 };
        if (auth_service->authenticate(auth_service->self, &root, &password, &authenticated) == FACET_OK &&
            security_manager->create_session(security_manager->self, authenticated, &session) == FACET_OK)
            child_log(logger, "authenticated root session ready");
        else child_log(logger, "root authentication/session failed");
    } else child_log(logger, "auth/security bindings unavailable");
    libfacet_free_proxy_client(auth_service);
    libfacet_free_proxy_client(security_manager);

    /* A terminal is delegated only to domains that own one.  This confirms
     * that normal user-facing output reaches COM1, independently of klog's
     * Bochs debug sink. */
    FacetString stdout_name = { .data = "stdout", .length = 6 };
    FacetHandle stdout_handle = {0};
    if (process_environment->resolve(process_environment->self, &stdout_name,
                                     &stdout_handle) == FACET_OK) {
        IByteWriter *stdout_stream = libfacet_proxy_from_handle(
            &IByteWriter_MetaData, stdout_handle);
        if (stdout_stream != NULL) {
            static const uint8_t greeting[] =
                "FacetOS serial terminal ready\r\n";
            FacetArray_u8 bytes = {
                .data = (uint8_t *)(uintptr_t)greeting,
                .count = sizeof(greeting) - 1,
            };
            uint32_t written;
            (void)stdout_stream->write_bytes(stdout_stream->self, &bytes,
                                             &written);
        }
        libfacet_free_proxy_client(stdout_stream);
    }

    libfacet_free_proxy_client(environment);
    libfacet_free_proxy_client(process_environment);
    libfacet_free_proxy_client(logger);
    libfacet_free_proxy_client(root);

    /* allocator.c retains this proxy for every later liballoc page request. */

    for (;;)
        (void)platform_yield();
}
