#include <facetos/config.h>
#include <facetos/dominit0/auth.h>
#include <facetos/dominit0/config.h>
#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/logging.h>
#include <facetos/interfaces/IAuthService.h>
#include <facetos/interfaces/IDomainEnvironment.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IHumanUser.h>
#include <facetos/interfaces/IPrincipal.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/ISecurityManager.h>
#include <facetos/interfaces/ISession.h>
#include <facetos/libfacet/platform.h>

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct FakeHandle {
    void *context;
    FacetPlatformDispatch dispatch;
    size_t references;
} FakeHandle;

FacetResult libfacet_platform_export(void *context, FacetPlatformDispatch dispatch,
                                     FacetHandle *out)
{
    FakeHandle *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) return FACET_OUT_OF_MEMORY;
    handle->context = context;
    handle->dispatch = dispatch;
    handle->references = 1;
    out->platform = handle;
    return FACET_OK;
}

FacetResult libfacet_platform_unexport(FacetHandle handle)
{
    return libfacet_platform_handle_release(handle);
}

FacetResult libfacet_platform_handle_clone(FacetHandle source,
                                           FacetHandle *destination)
{
    if (source.platform == NULL || destination == NULL)
        return FACET_INVALID_HANDLE;
    FakeHandle *handle = source.platform;
    handle->references++;
    destination->platform = handle;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_release(FacetHandle source)
{
    FakeHandle *handle = source.platform;
    if (handle == NULL || handle->references == 0) return FACET_INVALID_HANDLE;
    if (--handle->references == 0) free(handle);
    return FACET_OK;
}

FacetResult libfacet_platform_call(FacetHandle target,
                                   const FacetRpcMessage *request,
                                   FacetRpcMessage *reply)
{
    FakeHandle *handle = target.platform;
    if (handle == NULL || request == NULL || reply == NULL)
        return FACET_INVALID_ARGUMENT;
    FacetRpcMessage generated = {.protocol_version = FACET_RPC_PROTOCOL_VERSION};
    FacetResult result = handle->dispatch(handle->context, request, &generated);
    if (generated.word_count == 0)
        generated.words[generated.word_count++] = (uint64_t)(int64_t)result;
    for (size_t i = 0; i < generated.attachment_count; i++) {
        if (generated.attachments[i].handle.platform == NULL) continue;
        FacetHandle clone = {0};
        if (libfacet_platform_handle_clone(generated.attachments[i].handle,
                                           &clone) != FACET_OK)
            return FACET_OUT_OF_MEMORY;
        generated.attachments[i].handle = clone;
    }
    *reply = generated;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_from(uint64_t value, FacetHandle *out)
{
    (void)value;
    (void)out;
    return FACET_NOT_SUPPORTED;
}

FacetResult libfacet_platform_method_handle(FacetHandle object,
                                            uint32_t method_id,
                                            FacetHandle *out)
{
    (void)object;
    (void)method_id;
    (void)out;
    return FACET_NOT_SUPPORTED;
}

void klog(enum log_level level, const char *format, ...)
{
    (void)level;
    (void)format;
}

FacetResult dominit0_logging_emit(ILoggingConfig *config, uint64_t domain_id,
                                  int32_t level, FacetString component,
                                  FacetString message)
{
    (void)config;
    (void)domain_id;
    (void)level;
    (void)component;
    (void)message;
    return FACET_OK;
}

static IProcessEnvironment *domain_process_environment(
    Dominit0SystemConfig *system, size_t domain_index)
{
    FacetHandle root_handle = dominit0_environment_root_handle(
        system->current_domains[domain_index]->environment);
    FacetHandle root_copy = {0};
    assert(libfacet_handle_clone(root_handle, &root_copy) == FACET_OK);
    IDomainEnvironment *root = libfacet_new_proxy_client(
        &IDomainEnvironment_MetaData, root_copy);
    assert(root != NULL);
    FacetHandle process_handle = {0};
    assert(root->getInterface(root->self, IID_IProcessEnvironment,
                              &process_handle) == FACET_OK);
    libfacet_free_proxy_client(root);
    IProcessEnvironment *environment = libfacet_new_proxy_client(
        &IProcessEnvironment_MetaData, process_handle);
    assert(environment != NULL);
    return environment;
}

static void *resolve(IProcessEnvironment *environment, const char *name,
                     const FacetInterfaceMeta *metadata)
{
    FacetString key = {.data = name, .length = strlen(name)};
    FacetHandle handle = {0};
    assert(environment->resolve(environment->self, &key, &handle) == FACET_OK);
    void *object = libfacet_new_proxy_client(metadata, handle);
    assert(object != NULL);
    return object;
}

int main(void)
{
    assert(libfacet_register_generic_metadata(&IGenericObject_MetaData) ==
           FACET_OK);
    assert(libfacet_register_interface_metadata(&IHumanUser_MetaData) ==
           FACET_OK);
    FacetSystemConfig parsed;
    FacetConfigDiagnostic diagnostic;
    Dominit0SystemConfig system;
    assert(facet_config_make_fallback(&parsed, &diagnostic) == 0);
    assert(dominit0_config_objects_init(&system, &parsed, &diagnostic) == 0);
    assert(dominit0_environment_initialize(&system) == 0);
    assert(dominit0_auth_initialize(&system) == 0);

    IProcessEnvironment *domain0 = domain_process_environment(&system, 0);
    IAuthService *auth = resolve(domain0, "auth", &IAuthService_MetaData);
    ISecurityManager *security0 = resolve(domain0, "security",
                                          &ISecurityManager_MetaData);

    FacetString name = {.data = "root", .length = 4};
    FacetString wrong = {.data = "wrong", .length = 5};
    FacetHandle proof = {0};
    assert(auth->authenticate(auth->self, &name, &wrong, &proof) ==
           FACET_ACCESS_DENIED);
    assert(proof.platform == NULL);

    FacetString password = {.data = "facetos", .length = 7};
    assert(auth->authenticate(auth->self, &name, &password, &proof) == FACET_OK);
    assert(proof.platform != NULL);

    IProcessEnvironment *domain1 = domain_process_environment(&system, 1);
    ISecurityManager *security1 = resolve(domain1, "security",
                                          &ISecurityManager_MetaData);
    FacetHandle rejected = {0};
    assert(security1->create_session(security1->self, proof, &rejected) ==
           FACET_ACCESS_DENIED);
    assert(rejected.platform == NULL);

    FacetHandle session_handle = {0};
    assert(security0->create_session(security0->self, proof, &session_handle) ==
           FACET_OK);
    ISession *session = libfacet_new_proxy_client(&ISession_MetaData,
                                                  session_handle);
    assert(session != NULL);
    uint64_t domain_id = UINT64_MAX;
    assert(session->get_domain_id(session->self, &domain_id) == FACET_OK);
    assert(domain_id == 0);
    FacetHandle principal_handle = {0};
    assert(session->get_principal(session->self, &principal_handle) == FACET_OK);
    IPrincipal *principal = libfacet_new_proxy_client(&IPrincipal_MetaData,
                                                      principal_handle);
    FacetString returned_name = {0};
    assert(principal->getname(principal->self, &returned_name) == FACET_OK);
    assert(returned_name.length == name.length);
    assert(memcmp(returned_name.data, name.data, name.length) == 0);
    free((void *)(uintptr_t)returned_name.data);
    IHumanUser *human = (IHumanUser *)libfacet_proxy_client_get_interface(
        principal, IID_IHumanUser);
    assert(human != NULL);
    FacetString shell = {0};
    assert(human->getdefault_shell(human->self, &shell) == FACET_OK);
    assert(shell.length == strlen("/FacetOS/FacetShell"));
    assert(memcmp(shell.data, "/FacetOS/FacetShell", shell.length) == 0);
    free((void *)(uintptr_t)shell.data);

    libfacet_free_proxy_client(human);
    libfacet_free_proxy_client(principal);
    libfacet_free_proxy_client(session);
    (void)libfacet_handle_release(proof);
    libfacet_free_proxy_client(security1);
    libfacet_free_proxy_client(domain1);
    libfacet_free_proxy_client(security0);
    libfacet_free_proxy_client(auth);
    libfacet_free_proxy_client(domain0);
    dominit0_auth_destroy();
    dominit0_environment_destroy(&system);
    dominit0_config_objects_destroy(&system);
    return 0;
}
