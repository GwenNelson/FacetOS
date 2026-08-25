#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/logging.h>

#include <facetos/interfaces/IDomainEnvironment.h>
#include <facetos/interfaces/ILogger.h>
#include <facetos/interfaces/IPageAllocator.h>

#include <stdlib.h>
#include <string.h>

struct Dominit0DomainEnvironment {
    IDomainEnvironment interface;
    ILogger logger;
    IDomainConfig *config;
    FacetHandle environment_handle;
    FacetHandle logger_handle;
    FacetHandle page_allocator_handle;
};

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult return_handle(FacetHandle handle, FacetHandle *result)
{
    if (result == NULL)
        return FACET_INVALID_ARGUMENT;
    *result = (FacetHandle){0};
    if (handle.platform == NULL)
        return FACET_INVALID_HANDLE;
    *result = handle;
    return FACET_OK;
}

static FacetResult environment_get_interface(void *self, uuid_t iid,
                                             FacetHandle *result)
{
    Dominit0DomainEnvironment *environment = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_IDomainEnvironment))
        return return_handle(environment->environment_handle, result);
    if (iid_equal(iid, IID_ILogger))
        return return_handle(environment->logger_handle, result);
    if (iid_equal(iid, IID_IPageAllocator))
        return return_handle(environment->page_allocator_handle, result);
    if (result != NULL)
        *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult environment_get_domain_id(void *self, uint64_t *value)
{
    Dominit0DomainEnvironment *environment = self;
    return environment->config->getdomain_id(environment->config->self, value);
}

static FacetResult environment_get_domain_name(void *self, FacetString *value)
{
    Dominit0DomainEnvironment *environment = self;
    return environment->config->getdomain_name(environment->config->self, value);
}

static FacetResult environment_get_personality(void *self, uint32_t *value)
{
    Dominit0DomainEnvironment *environment = self;
    Personality personality;
    FacetResult result = environment->config->getpersonality(
        environment->config->self, &personality);
    if (result == FACET_OK && value != NULL)
        *value = (uint32_t)personality;
    return result;
}

static FacetResult logger_get_interface(void *self, uuid_t iid,
                                        FacetHandle *result)
{
    Dominit0DomainEnvironment *environment = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ILogger))
        return return_handle(environment->logger_handle, result);
    if (result != NULL)
        *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult logger_log(void *self, int32_t level, uint32_t event)
{
    Dominit0DomainEnvironment *environment = self;
    static const char *const messages[] = {
        NULL,
        "received environment IPageAllocator",
        "activated IPageAllocator",
        "IPageAllocator/liballoc ready",
        "could not activate IPageAllocator",
        "could not allocate through IPageAllocator",
    };
    if (event == 0 || event >= sizeof(messages) / sizeof(messages[0]))
        return FACET_INVALID_ARGUMENT;
    uint64_t domain_id;
    FacetResult result = environment_get_domain_id(environment, &domain_id);
    if (result != FACET_OK)
        return result;
    Dominit0DomainConfigObject *config = environment->config->self;
    FacetString component = { .data = "dominit", .length = 7 };
    FacetString message = { .data = messages[event], .length = 0 };
    while (messages[event][message.length] != '\0')
        message.length++;
    return dominit0_logging_emit(&config->logging, domain_id, level,
                                 component, message);
}

static FacetResult logger_flush(void *self)
{
    (void)self;
    return FACET_OK;
}

int dominit0_environment_initialize(Dominit0SystemConfig *system)
{
    if (system == NULL || system->domains == NULL ||
        system->current_domains == NULL)
        return -1;

    for (size_t i = 0; i < system->domain_count; i++) {
        CurrentDomain *current = system->current_domains[i];
        if (current == NULL || current->config != &system->domains[i].domain) {
            klog(LOG_ERROR, "environment %zu has invalid domain state\n", i);
            goto fail;
        }
        Dominit0DomainEnvironment *environment = calloc(1, sizeof(*environment));
        if (environment == NULL) {
            klog(LOG_ERROR, "environment %zu allocation failed\n", i);
            goto fail;
        }
        environment->config = current->config;
        environment->interface.self = environment;
        environment->interface.priv = environment;
        environment->interface.getInterface = environment_get_interface;
        environment->interface.getdomain_id = environment_get_domain_id;
        environment->interface.getdomain_name = environment_get_domain_name;
        environment->interface.getpersonality = environment_get_personality;
        environment->logger.self = environment;
        environment->logger.priv = environment;
        environment->logger.getInterface = logger_get_interface;
        environment->logger.log = logger_log;
        environment->logger.flush = logger_flush;

        if (libfacet_export_interface(&environment->logger, &ILogger_MetaData,
                                      &environment->logger_handle) != FACET_OK ||
            libfacet_export_interface(&environment->interface,
                                      &IDomainEnvironment_MetaData,
                                      &environment->environment_handle) != FACET_OK) {
            if (environment->logger_handle.platform != NULL)
                (void)libfacet_unexport_interface(environment->logger_handle);
            klog(LOG_ERROR, "environment %zu could not export root/logger\n", i);
            free(environment);
            goto fail;
        }
        current->environment = environment;
    }
    return 0;

fail:
    dominit0_environment_destroy(system);
    return -1;
}

void dominit0_environment_destroy(Dominit0SystemConfig *system)
{
    if (system == NULL || system->current_domains == NULL)
        return;
    for (size_t i = 0; i < system->domain_count; i++) {
        CurrentDomain *current = system->current_domains[i];
        if (current == NULL || current->environment == NULL)
            continue;
        Dominit0DomainEnvironment *environment = current->environment;
        if (environment->environment_handle.platform != NULL)
            (void)libfacet_unexport_interface(environment->environment_handle);
        if (environment->logger_handle.platform != NULL)
            (void)libfacet_unexport_interface(environment->logger_handle);
        free(environment);
        current->environment = NULL;
    }
}

int dominit0_environment_bind_page_allocator(Dominit0DomainEnvironment *environment,
                                              FacetHandle page_allocator)
{
    if (environment == NULL || page_allocator.platform == NULL ||
        environment->page_allocator_handle.platform != NULL)
        return -1;
    environment->page_allocator_handle = page_allocator;
    return 0;
}

FacetHandle dominit0_environment_root_handle(
    const Dominit0DomainEnvironment *environment)
{
    return environment == NULL ? (FacetHandle){0} : environment->environment_handle;
}
