#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IDomainConfig.h>
#include <facetos/interfaces/IDomainConsoleConfig.h>
#include <facetos/interfaces/IDomainEnvironment.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/ILogger.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcess.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IProcessManager.h>
#include <facetos/interfaces/IDomainPosixPolicy.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/allocator.h"

static FacetResult child_log(ILogger *logger, const char *text)
{
    FacetString message = {.data = text, .length = strlen(text)};
    return logger->log(logger->self, 40, &message);
}

static void *resolve(IProcessEnvironment *environment, const char *name,
                     const FacetInterfaceMeta *metadata)
{
    if (environment == NULL) return NULL;
    FacetString key = {.data = name, .length = strlen(name)};
    FacetHandle handle = {0};
    if (environment->resolve(environment->self, &key, &handle) != FACET_OK)
        return NULL;
    return libfacet_proxy_from_handle(metadata, handle);
}

static FacetResult configured_initial_process(IDomainEnvironment *environment,
                                              uint64_t index,
                                              FacetString *path,
                                              bool *posix_profile)
{
    if (posix_profile == NULL) return FACET_INVALID_ARGUMENT;
    *posix_profile = false;
    FacetHandle config_handle = {0};
    if (environment->getdomain_config(environment->self, &config_handle) != FACET_OK)
        return FACET_INVALID_HANDLE;
    IDomainConfig *config = libfacet_proxy_from_handle(&IDomainConfig_MetaData,
                                                       config_handle);
    FacetHandle console_handle = {0};
    FacetResult result = config == NULL ? FACET_INVALID_HANDLE :
        config->getconsole_config(config->self, &console_handle);
    libfacet_free_proxy_client(config);
    if (result != FACET_OK) return result;
    IDomainConsoleConfig *console = libfacet_proxy_from_handle(
        &IDomainConsoleConfig_MetaData, console_handle);
    Assignment assignment = {0};
    uint64_t assignment_count = 0;
    result = console == NULL ? FACET_INVALID_HANDLE :
        console->getassignment_count(console->self, &assignment_count);
    if (result == FACET_OK && index < assignment_count)
        result = console->get_assignment(console->self, index, &assignment);
    libfacet_free_proxy_client(console);
    if (result != FACET_OK || index >= assignment_count ||
        assignment.initial_process.data == NULL ||
        assignment.initial_process.length == 0) {
        facet_rpc_release_value(FACET_TYPE_STRUCT,
                                &Assignment_TypeMeta, &assignment);
        return result == FACET_OK ? FACET_NOT_FOUND : result;
    }
    /* Move the selected decoded string to the caller. dominit is a permanent
     * supervisor and performs this once, so retaining the three other small
     * decoded fields avoids allocator RPCs in the bootstrap path. */
    *path = assignment.initial_process;
    *posix_profile = assignment.view.data != NULL &&
        assignment.view.length == sizeof("posix") - 1 &&
        memcmp(assignment.view.data, "posix", assignment.view.length) == 0;
    return FACET_OK;
}

static uint64_t configured_assignment_count(IDomainEnvironment *environment)
{
    FacetHandle config_handle = {0};
    if (environment->getdomain_config(environment->self, &config_handle) != FACET_OK)
        return 0;
    IDomainConfig *config = libfacet_proxy_from_handle(&IDomainConfig_MetaData,
                                                       config_handle);
    FacetHandle console_handle = {0};
    FacetResult result = config == NULL ? FACET_INVALID_HANDLE :
        config->getconsole_config(config->self, &console_handle);
    libfacet_free_proxy_client(config);
    IDomainConsoleConfig *console = result == FACET_OK ?
        libfacet_proxy_from_handle(&IDomainConsoleConfig_MetaData,
                                   console_handle) : NULL;
    uint64_t count = 0;
    if (console != NULL)
        (void)console->getassignment_count(console->self, &count);
    libfacet_free_proxy_client(console);
    return count;
}

static FacetResult configured_pid1(IDomainEnvironment *environment,
                                   FacetString *path)
{
    FacetHandle config_handle = {0};
    if (environment->getdomain_config(environment->self, &config_handle) != FACET_OK)
        return FACET_INVALID_HANDLE;
    IDomainConfig *config = libfacet_proxy_from_handle(&IDomainConfig_MetaData,
                                                       config_handle);
    FacetResult result = config == NULL ? FACET_INVALID_HANDLE :
        config->getpid1(config->self, path);
    libfacet_free_proxy_client(config);
    return result == FACET_OK && path->data != NULL && path->length != 0 ?
        FACET_OK : FACET_NOT_FOUND;
}

typedef struct DomainPosixPolicy {
    IDomainPosixPolicy interface;
    FacetHandle handle;
    const char *backing_root;
    bool synthetic_etc;
} DomainPosixPolicy;

static FacetResult policy_get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    DomainPosixPolicy *policy = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (memcmp(iid.bytes, IID_IGenericObject.bytes, sizeof(iid.bytes)) != 0 &&
        memcmp(iid.bytes, IID_IDomainPosixPolicy.bytes, sizeof(iid.bytes)) != 0)
        return FACET_NO_INTERFACE;
    *out = policy->handle;
    return FACET_OK;
}

static FacetResult policy_get_backing_root(void *self, FacetString *out)
{
    DomainPosixPolicy *policy = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    out->data = policy->backing_root;
    out->length = strlen(policy->backing_root);
    return FACET_OK;
}

static FacetResult policy_get_synthetic_etc(void *self, bool *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = ((DomainPosixPolicy *)self)->synthetic_etc;
    return FACET_OK;
}

static FacetResult publish_posix_policy(IDomainEnvironment *environment,
                                        Personality personality)
{
    static DomainPosixPolicy policy;
    policy.backing_root = personality == Personality_Native ? "/posix" : "/";
    policy.synthetic_etc = personality == Personality_Native;
    policy.interface.self = &policy;
    policy.interface.priv = &policy;
    policy.interface.getInterface = policy_get_interface;
    policy.interface.getbacking_root = policy_get_backing_root;
    policy.interface.getsynthetic_etc = policy_get_synthetic_etc;
    if (libfacet_export_interface(&policy.interface, &IDomainPosixPolicy_MetaData,
                                  &policy.handle) != FACET_OK)
        return FACET_OUT_OF_MEMORY;
    FacetString name = {.data = "posix.policy", .length = 12};
    FacetResult result = environment->publish_service(environment->self, &name,
        IID_IDomainPosixPolicy, policy.handle);
    if (result != FACET_OK) (void)libfacet_unexport_interface(policy.handle);
    return result;
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDomainEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ILogger_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDomainConfig_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDomainConsoleConfig_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessManager_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDomainPosixPolicy_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcess_MetaData) != FACET_OK)
        return 1;

    IGenericObject *root = NULL;
    if (platform_init(&argc, &argv, &root) != FACET_OK) return 1;
    IDomainEnvironment *domain =
        (IDomainEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IDomainEnvironment);
    IProcessEnvironment *environment =
        (IProcessEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IProcessEnvironment);
    ILogger *logger = resolve(environment, "logger", &ILogger_MetaData);
    IPageAllocator *allocator = resolve(environment, "memory.pages",
                                        &IPageAllocator_MetaData);
    if (domain == NULL || environment == NULL || logger == NULL ||
        allocator == NULL)
        return 1;
    (void)logger->flush(logger->self);
    if (dominit_allocator_use_pages(allocator) != 0) return 1;
    child_log(logger, "dominit allocator ready");

    IProcessManager *processes = resolve(environment, "processes",
                                         &IProcessManager_MetaData);
    if (processes != NULL) {
        FacetHandle config_handle = {0};
        IDomainConfig *config = domain->getdomain_config(domain->self,
                                                           &config_handle) == FACET_OK ?
            libfacet_proxy_from_handle(&IDomainConfig_MetaData, config_handle) : NULL;
        Personality personality = Personality_Native;
        if (config != NULL) (void)config->getpersonality(config->self, &personality);
        libfacet_free_proxy_client(config);
        if (publish_posix_policy(domain, personality) != FACET_OK)
            child_log(logger, "could not publish POSIX policy");
        FacetString posix_root = {
            .data = personality == Personality_Native ? "/posix" : "/",
            .length = personality == Personality_Native ? 6 : 1,
        };
        if (personality == Personality_Posix) {
            FacetString pid1 = {0};
            FacetHandle process = {0};
            if (configured_pid1(domain, &pid1) == FACET_OK) {
                FacetString args[] = {pid1};
                FacetArray_string argv = {.data = args, .count = 1};
                if (processes->launch_initial(processes->self, &pid1, &argv, 0,
                                              &posix_root,
                                              true,
                                              &process) == FACET_OK)
                    child_log(logger, "launched POSIX pid1");
                if (process.platform != NULL) (void)libfacet_handle_release(process);
            }
        } else {
        uint64_t assignment_count = configured_assignment_count(domain);
        for (uint64_t index = 0; index < assignment_count; index++) {
            FacetString initial = {0};
            FacetHandle process = {0};
            bool posix_profile = false;
            if (configured_initial_process(domain, index, &initial, &posix_profile) !=
                FACET_OK)
                continue;
            FacetString argument = initial;
            char index_text[24];
            size_t index_length = 0;
            uint64_t value = index;
            do {
                index_text[index_length++] = (char)('0' + value % 10);
                value /= 10;
            } while (value != 0);
            for (size_t left = 0, right = index_length - 1;
                 left < right; left++, right--) {
                char temporary = index_text[left];
                index_text[left] = index_text[right];
                index_text[right] = temporary;
            }
            FacetString index_argument = {
                .data = index_text,
                .length = index_length,
            };
            FacetString argument_values[] = {argument, index_argument};
            FacetArray_string arguments = {
                .data = argument_values,
                .count = 2,
            };
            if (processes->launch_initial(processes->self, &initial, &arguments,
                                          index,
                                          &posix_root,
                                          posix_profile,
                                          &process) == FACET_OK)
                child_log(logger, "launched configured initial process");
            else
                child_log(logger, "could not launch configured initial process");
            if (process.platform != NULL)
                (void)libfacet_handle_release(process);
        }
        }
    } else {
        child_log(logger, "no local process manager configured");
    }

    libfacet_free_proxy_client(processes);
    /* allocator.c retains allocator for all later liballoc page requests.
     * Dominit now services its domain-owned bootstrap interfaces. */
    return platform_run_services() == FACET_OK ? 0 : 1;
}
