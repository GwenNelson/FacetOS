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
#include <facetos/interfaces/IPrincipal.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/allocator.h"

static FacetResult child_log(ILogger *logger, const char *text)
{
    FacetString message = { .data = text, .length = 0 };
    while (text[message.length] != '\0')
        message.length++;
    return logger->log(logger->self, 40, &message);
}

static FacetResult stream_write(IByteWriter *stream, const char *text)
{
    FacetArray_u8 bytes = {
        .data = (uint8_t *)(uintptr_t)text,
        .count = strlen(text),
    };
    uint32_t written = 0;
    FacetResult result = stream->write_bytes(stream->self, &bytes, &written);
    return result == FACET_OK && written == bytes.count ? FACET_OK : FACET_ERROR;
}

static FacetResult stream_read_line(IByteReader *input, IByteWriter *output,
                                    char *buffer, size_t capacity, int echo)
{
    if (capacity == 0) return FACET_INVALID_ARGUMENT;
    size_t length = 0;
    for (;;) {
        FacetArray_u8 bytes = {0};
        FacetResult result = input->read_bytes(input->self, 1, &bytes);
        if (result != FACET_OK) return result;
        if (bytes.count == 0) {
            free(bytes.data);
            (void)platform_yield();
            continue;
        }
        uint8_t byte = bytes.data[0];
        free(bytes.data);
        if (byte == '\r' || byte == '\n') {
            buffer[length] = '\0';
            (void)stream_write(output, "\r\n");
            return FACET_OK;
        }
        if (byte == 8 || byte == 127) {
            if (length != 0) {
                length--;
                if (echo) (void)stream_write(output, "\b \b");
            }
            continue;
        }
        if (byte < 32 || byte > 126 || length + 1 >= capacity) continue;
        buffer[length++] = (char)byte;
        if (echo) {
            char displayed[2] = {(char)byte, '\0'};
            (void)stream_write(output, displayed);
        }
    }
}

static void clear_secret(char *buffer, size_t capacity)
{
    volatile char *bytes = buffer;
    while (capacity-- != 0) *bytes++ = 0;
}

static void serial_login(IByteReader *input, IByteWriter *output,
                         IAuthService *auth, ISecurityManager *security)
{
    char username[128];
    char password[256];
    (void)stream_write(output, "FacetOS native login\r\n");
    for (;;) {
        (void)stream_write(output, "login: ");
        if (stream_read_line(input, output, username, sizeof(username), 1) != FACET_OK)
            return;
        (void)stream_write(output, "password: ");
        if (stream_read_line(input, output, password, sizeof(password), 0) != FACET_OK) {
            clear_secret(password, sizeof(password));
            return;
        }
        FacetString name = {.data = username, .length = strlen(username)};
        FacetString secret = {.data = password, .length = strlen(password)};
        FacetHandle authenticated = {0};
        FacetHandle session = {0};
        FacetResult result = auth->authenticate(auth->self, &name, &secret,
                                                &authenticated);
        clear_secret(password, sizeof(password));
        if (result == FACET_OK)
            result = security->create_session(security->self, authenticated,
                                              &session);
        if (authenticated.platform != NULL)
            (void)libfacet_handle_release(authenticated);
        if (result != FACET_OK) {
            (void)stream_write(output, "Login incorrect\r\n");
            continue;
        }
        (void)stream_write(output, "Authenticated session ready; shell launch is not implemented yet.\r\n");
        if (session.platform != NULL) (void)libfacet_handle_release(session);
    }
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
        libfacet_register_interface_metadata(&IPrincipal_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteReader_MetaData) != FACET_OK ||
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
    FacetHandle auth_handle = {0}, security_handle = {0};
    IAuthService *auth_service = NULL;
    ISecurityManager *security_manager = NULL;
    if (process_environment->resolve(process_environment->self, &auth_name, &auth_handle) == FACET_OK &&
        process_environment->resolve(process_environment->self, &security_name, &security_handle) == FACET_OK) {
        auth_service = libfacet_proxy_from_handle(&IAuthService_MetaData, auth_handle);
        security_manager = libfacet_proxy_from_handle(&ISecurityManager_MetaData, security_handle);
    }
    /* A terminal is delegated only to domains that own one. */
    FacetString stdin_name = { .data = "stdin", .length = 5 };
    FacetString stdout_name = { .data = "stdout", .length = 6 };
    FacetHandle stdin_handle = {0}, stdout_handle = {0};
    IByteReader *stdin_stream = NULL;
    IByteWriter *stdout_stream = NULL;
    if (process_environment->resolve(process_environment->self, &stdin_name,
                                     &stdin_handle) == FACET_OK &&
        process_environment->resolve(process_environment->self, &stdout_name,
                                     &stdout_handle) == FACET_OK) {
        stdin_stream = libfacet_proxy_from_handle(&IByteReader_MetaData,
                                                  stdin_handle);
        stdout_stream = libfacet_proxy_from_handle(&IByteWriter_MetaData,
                                                   stdout_handle);
    }

    if (stdin_stream != NULL && stdout_stream != NULL &&
        auth_service != NULL && security_manager != NULL)
        serial_login(stdin_stream, stdout_stream, auth_service,
                     security_manager);
    else if (stdin_stream != NULL || stdout_stream != NULL)
        child_log(logger, "terminal login bindings incomplete");

    libfacet_free_proxy_client(stdin_stream);
    libfacet_free_proxy_client(stdout_stream);
    libfacet_free_proxy_client(auth_service);
    libfacet_free_proxy_client(security_manager);

    libfacet_free_proxy_client(environment);
    libfacet_free_proxy_client(process_environment);
    libfacet_free_proxy_client(logger);
    libfacet_free_proxy_client(root);

    /* allocator.c retains this proxy for every later liballoc page request. */

    for (;;)
        (void)platform_yield();
}
