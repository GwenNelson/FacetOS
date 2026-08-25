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
                                              ILogger *logger,
                                              FacetString *path)
{
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
    if (result == FACET_OK && assignment_count != 0)
        result = console->get_assignment(console->self, 0, &assignment);
    libfacet_free_proxy_client(console);
    if (result != FACET_OK || assignment_count == 0 ||
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
    return FACET_OK;
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
        FacetString initial = {0};
        FacetHandle process = {0};
        if (configured_initial_process(domain, logger, &initial) == FACET_OK) {
            FacetString argument = initial;
            FacetArray_string arguments = {.data = &argument, .count = 1};
            if (processes->launch_initial(processes->self, &initial, &arguments,
                                          &process) == FACET_OK)
                child_log(logger, "launched configured initial process");
            else
                child_log(logger, "could not launch configured initial process");
        }
        if (process.platform != NULL) (void)libfacet_handle_release(process);
    } else {
        child_log(logger, "no local process manager configured");
    }

    libfacet_free_proxy_client(processes);
    /* allocator.c retains allocator for all later liballoc page requests. */
    for (;;) (void)platform_yield();
}
