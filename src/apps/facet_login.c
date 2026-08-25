#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IAuthService.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IHumanUser.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IPrincipal.h>
#include <facetos/interfaces/IProcess.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IProcessManager.h>
#include <facetos/interfaces/ISecurityManager.h>
#include <facetos/interfaces/ISession.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../dominit/platform/allocator.h"

static FacetResult write_text(IByteWriter *output, const char *text)
{
    FacetArray_u8 bytes = {
        .data = (uint8_t *)(uintptr_t)text,
        .count = strlen(text),
    };
    uint32_t written = 0;
    FacetResult result = output->write_bytes(output->self, &bytes, &written);
    return result == FACET_OK && written == bytes.count ? FACET_OK : FACET_ERROR;
}

static FacetResult read_line(IByteReader *input, IByteWriter *output,
                             char *buffer, size_t capacity, bool echo)
{
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
            (void)write_text(output, "\r\n");
            return FACET_OK;
        }
        if (byte == 8 || byte == 127) {
            if (length != 0) {
                length--;
                if (echo) (void)write_text(output, "\b \b");
            }
            continue;
        }
        if (byte < 32 || byte > 126 || length + 1 >= capacity) continue;
        buffer[length++] = (char)byte;
        if (echo) {
            char displayed[2] = {(char)byte, '\0'};
            (void)write_text(output, displayed);
        }
    }
}

static void clear_secret(char *buffer, size_t capacity)
{
    volatile char *bytes = buffer;
    while (capacity-- != 0) *bytes++ = 0;
}

static FacetString shell_for_session(FacetHandle session_handle, char **owned)
{
    FacetString shell = {
        .data = "/FacetOS/FacetShell",
        .length = sizeof("/FacetOS/FacetShell") - 1,
    };
    *owned = NULL;
    FacetHandle session_copy = {0};
    if (libfacet_handle_clone(session_handle, &session_copy) != FACET_OK)
        return shell;
    ISession *session = libfacet_proxy_from_handle(&ISession_MetaData,
                                                   session_copy);
    FacetHandle principal_handle = {0};
    if (session == NULL ||
        session->get_principal(session->self, &principal_handle) != FACET_OK) {
        libfacet_free_proxy_client(session);
        if (session == NULL) (void)libfacet_handle_release(session_copy);
        return shell;
    }
    IPrincipal *principal = libfacet_proxy_from_handle(&IPrincipal_MetaData,
                                                       principal_handle);
    IHumanUser *human = principal == NULL ? NULL :
        (IHumanUser *)libfacet_proxy_client_get_interface(principal,
                                                          IID_IHumanUser);
    FacetString configured = {0};
    if (human != NULL &&
        human->getdefault_shell(human->self, &configured) == FACET_OK &&
        configured.data != NULL && configured.length != 0) {
        shell = configured;
        *owned = (char *)(uintptr_t)configured.data;
    }
    libfacet_free_proxy_client(human);
    libfacet_free_proxy_client(principal);
    libfacet_free_proxy_client(session);
    return shell;
}

static void login_loop(IByteReader *input, IByteWriter *output,
                       IAuthService *auth, ISecurityManager *security,
                       IProcessManager *processes)
{
    char username[128];
    char password[256];
    (void)write_text(output, "FacetOS native login\r\n");
    for (;;) {
        (void)write_text(output, "login: ");
        if (read_line(input, output, username, sizeof(username), true) != FACET_OK)
            return;
        (void)write_text(output, "password: ");
        if (read_line(input, output, password, sizeof(password), false) != FACET_OK) {
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
            (void)write_text(output, "Login incorrect\r\n");
            continue;
        }
        char *owned_shell = NULL;
        FacetString shell = shell_for_session(session, &owned_shell);
        FacetString argument = shell;
        FacetArray_string arguments = {.data = &argument, .count = 1};
        FacetHandle process = {0};
        result = processes->launch(processes->self, &shell, &arguments,
                                   session, &process);
        free(owned_shell);
        if (result == FACET_OK) {
            (void)write_text(output, "Starting session...\r\n");
            if (process.platform != NULL) (void)libfacet_handle_release(process);
            (void)libfacet_handle_release(session);
            return;
        }
        (void)write_text(output, "Unable to start the configured shell\r\n");
        (void)libfacet_handle_release(session);
    }
}

static void *resolve(IProcessEnvironment *environment, const char *name,
                     const FacetInterfaceMeta *metadata)
{
    FacetString key = {.data = name, .length = strlen(name)};
    FacetHandle handle = {0};
    if (environment->resolve(environment->self, &key, &handle) != FACET_OK)
        return NULL;
    return libfacet_proxy_from_handle(metadata, handle);
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteReader_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IAuthService_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISecurityManager_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISession_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPrincipal_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IHumanUser_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessManager_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcess_MetaData) != FACET_OK)
        return 1;
    IGenericObject *root = NULL;
    if (platform_init(&argc, &argv, &root) != FACET_OK) return 1;
    IProcessEnvironment *environment =
        (IProcessEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IProcessEnvironment);
    if (environment == NULL) return 1;
    IPageAllocator *allocator = resolve(environment, "memory.pages",
                                        &IPageAllocator_MetaData);
    IByteReader *input = resolve(environment, "stdin", &IByteReader_MetaData);
    IByteWriter *output = resolve(environment, "stdout", &IByteWriter_MetaData);
    IAuthService *auth = resolve(environment, "auth", &IAuthService_MetaData);
    ISecurityManager *security = resolve(environment, "security",
                                         &ISecurityManager_MetaData);
    IProcessManager *processes = resolve(environment, "processes",
                                         &IProcessManager_MetaData);
    if (allocator == NULL || input == NULL || output == NULL || auth == NULL ||
        security == NULL || processes == NULL ||
        dominit_allocator_use_pages(allocator) != 0)
        return 1;
    login_loop(input, output, auth, security, processes);
    return 0;
}
