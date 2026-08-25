#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/logging.h>

#include <facetos/interfaces/IDomainEnvironment.h>
#include <facetos/interfaces/ILogger.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IFileStore.h>

#include <stdlib.h>
#include <string.h>

struct Dominit0DomainEnvironment {
    IDomainEnvironment interface;
    IProcessEnvironment process_environment;
    ILogger logger;
    IDomainConfig *config;
    FacetHandle environment_handle;
    FacetHandle process_environment_handle;
    FacetHandle logger_handle;
    FacetHandle domain_config_handle;
    FacetHandle page_allocator_handle;
    FacetHandle file_store_handle;
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
    if (iid_equal(iid, IID_IProcessEnvironment))
        return return_handle(environment->process_environment_handle, result);
    if (result != NULL)
        *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static bool name_equal(const FacetString *name, const char *literal)
{
    size_t length = strlen(literal);
    return name != NULL && name->data != NULL && name->length == length &&
        memcmp(name->data, literal, length) == 0;
}

static FacetResult process_binding(Dominit0DomainEnvironment *environment,
                                   const FacetString *name, uuid_t *iid,
                                   FacetHandle *handle)
{
    if (name == NULL || name->data == NULL || name->length == 0 ||
        name->length > 127)
        return FACET_INVALID_ARGUMENT;
    if (name_equal(name, "logger")) {
        if (iid != NULL) *iid = IID_ILogger;
        if (handle != NULL) *handle = environment->logger_handle;
        return environment->logger_handle.platform == NULL ? FACET_INVALID_HANDLE : FACET_OK;
    }
    if (name_equal(name, "memory.pages")) {
        if (iid != NULL) *iid = IID_IPageAllocator;
        if (handle != NULL) *handle = environment->page_allocator_handle;
        return environment->page_allocator_handle.platform == NULL ? FACET_INVALID_HANDLE : FACET_OK;
    }
    if (name_equal(name, "domain.config")) {
        if (iid != NULL) *iid = IID_IDomainConfig;
        if (handle != NULL) *handle = environment->domain_config_handle;
        return environment->domain_config_handle.platform == NULL ? FACET_INVALID_HANDLE : FACET_OK;
    }
    if (name_equal(name, "files")) {
        if (iid != NULL) *iid = IID_IFileStore;
        if (handle != NULL) *handle = environment->file_store_handle;
        return environment->file_store_handle.platform == NULL ? FACET_INVALID_HANDLE : FACET_OK;
    }
    return FACET_NO_INTERFACE;
}

static FacetResult process_get_interface(void *self, uuid_t iid,
                                         FacetHandle *result)
{
    Dominit0DomainEnvironment *environment = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IProcessEnvironment))
        return return_handle(environment->process_environment_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult process_resolve(void *self, const FacetString *name,
                                   FacetHandle *result)
{
    if (result == NULL) return FACET_INVALID_ARGUMENT;
    *result = (FacetHandle){0};
    return process_binding(self, name, NULL, result);
}

static FacetResult process_resolve_as(void *self, const FacetString *name,
                                      uuid_t iid, FacetHandle *result)
{
    uuid_t primary = {0};
    FacetResult status = process_binding(self, name, &primary, result);
    if (status != FACET_OK) return status;
    if (!iid_equal(primary, iid)) {
        if (result != NULL) *result = (FacetHandle){0};
        return FACET_NO_INTERFACE;
    }
    return FACET_OK;
}

static FacetResult process_get_primary_iid(void *self, const FacetString *name,
                                           uuid_t *iid)
{
    if (iid == NULL) return FACET_INVALID_ARGUMENT;
    return process_binding(self, name, iid, NULL);
}

static FacetResult process_get_advertised_iids(void *self, const FacetString *name,
                                               FacetArray_uuid *iids)
{
    static uuid_t value;
    if (iids == NULL) return FACET_INVALID_ARGUMENT;
    iids->data = NULL;
    iids->count = 0;
    FacetResult status = process_binding(self, name, &value, NULL);
    if (status != FACET_OK) return status;
    iids->data = &value;
    iids->count = 1;
    return FACET_OK;
}

static FacetResult process_list_bindings(void *self, FacetArray_BindingInfo *bindings)
{
    static BindingInfo values[4];
    static const char *names[] = { "logger", "memory.pages", "domain.config", "files" };
    Dominit0DomainEnvironment *environment = self;
    if (bindings == NULL) return FACET_INVALID_ARGUMENT;
    for (size_t i = 0; i < 4; i++) {
        FacetString name = { .data = names[i], .length = strlen(names[i]) };
        if (process_binding(environment, &name, &values[i].primary_iid, NULL) != FACET_OK)
            return FACET_INVALID_HANDLE;
        values[i].name = name;
    }
    bindings->data = values;
    bindings->count = 4;
    return FACET_OK;
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

static FacetResult environment_get_domain_config(void *self, FacetHandle *value)
{
    return return_handle(((Dominit0DomainEnvironment *)self)->domain_config_handle,
                         value);
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

static FacetResult logger_log(void *self, int32_t level,
                              const FacetString *message)
{
    Dominit0DomainEnvironment *environment = self;
    if (message == NULL || (message->length != 0 && message->data == NULL))
        return FACET_INVALID_ARGUMENT;
    uint64_t domain_id;
    FacetResult result = environment_get_domain_id(environment, &domain_id);
    if (result != FACET_OK)
        return result;
    Dominit0DomainConfigObject *config = environment->config->self;
    FacetString component = { .data = "dominit", .length = 7 };
    return dominit0_logging_emit(&config->logging, domain_id, level,
                                 component, *message);
}

static FacetResult logger_flush(void *self)
{
    (void)self;
    return FACET_OK;
}

int dominit0_config_export_objects(Dominit0SystemConfig *system)
{
    if (system == NULL || system->domains == NULL)
        return -1;
    for (size_t i = 0; i < system->domain_count; i++) {
        /* The current platform service has a deliberately small exported
         * endpoint budget during bootstrap.  Export the root domain's
         * effective configuration now; child configuration views are kept
         * locally until the process-manager export pool is introduced. */
        if (i != system->root_index)
            continue;
        Dominit0DomainConfigObject *object = &system->domains[i];
        if (object->domain_handle.platform != NULL)
            continue;
        FacetHandle domain = {0};
        if (libfacet_export_interface(&object->domain, &IDomainConfig_MetaData,
                                      &domain) != FACET_OK) {
            klog(LOG_ERROR, "could not export domain config %zu\n", i);
            return -1;
        }
        /* The effective configuration is useful at bootstrap now.  Its
         * nested logger/console object views remain deliberately unbound
         * until their export lifetime is managed independently. */
        object->domain_handle = domain;
    }
    return 0;
}

int dominit0_environment_initialize(Dominit0SystemConfig *system)
{
    if (system == NULL || system->domains == NULL ||
        system->current_domains == NULL)
        return -1;
    if (dominit0_config_export_objects(system) != 0) {
        klog(LOG_ERROR, "could not export immutable domain configuration\n");
        return -1;
    }

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
        environment->domain_config_handle = system->domains[i].domain_handle;
        environment->interface.self = environment;
        environment->interface.priv = environment;
        environment->interface.getInterface = environment_get_interface;
        environment->interface.getdomain_id = environment_get_domain_id;
        environment->interface.getdomain_name = environment_get_domain_name;
        environment->interface.getpersonality = environment_get_personality;
        environment->interface.getdomain_config = environment_get_domain_config;
        environment->process_environment.self = environment;
        environment->process_environment.priv = environment;
        environment->process_environment.getInterface = process_get_interface;
        environment->process_environment.resolve = process_resolve;
        environment->process_environment.resolve_as = process_resolve_as;
        environment->process_environment.get_primary_iid = process_get_primary_iid;
        environment->process_environment.get_advertised_iids = process_get_advertised_iids;
        environment->process_environment.list_bindings = process_list_bindings;
        environment->logger.self = environment;
        environment->logger.priv = environment;
        environment->logger.getInterface = logger_get_interface;
        environment->logger.log = logger_log;
        environment->logger.flush = logger_flush;

        if (libfacet_export_interface(&environment->logger, &ILogger_MetaData,
                                      &environment->logger_handle) != FACET_OK ||
            libfacet_export_interface(&environment->process_environment,
                                      &IProcessEnvironment_MetaData,
                                      &environment->process_environment_handle) != FACET_OK ||
            libfacet_export_interface(&environment->interface,
                                      &IDomainEnvironment_MetaData,
                                      &environment->environment_handle) != FACET_OK) {
            if (environment->logger_handle.platform != NULL)
                (void)libfacet_unexport_interface(environment->logger_handle);
            if (environment->process_environment_handle.platform != NULL)
                (void)libfacet_unexport_interface(environment->process_environment_handle);
            klog(LOG_ERROR, "environment %zu could not export root/process environment/logger\n", i);
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
        if (environment->process_environment_handle.platform != NULL)
            (void)libfacet_unexport_interface(environment->process_environment_handle);
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

int dominit0_environment_bind_file_store(Dominit0DomainEnvironment *environment,
                                         FacetHandle file_store)
{
    if (environment == NULL || file_store.platform == NULL ||
        environment->file_store_handle.platform != NULL)
        return -1;
    environment->file_store_handle = file_store;
    return 0;
}

FacetHandle dominit0_environment_root_handle(
    const Dominit0DomainEnvironment *environment)
{
    return environment == NULL ? (FacetHandle){0} : environment->environment_handle;
}
