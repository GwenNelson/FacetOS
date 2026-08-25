#include <facetos/dominit0/environment.h>

#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/ISession.h>

#include <stdlib.h>
#include <string.h>

#define PROCESS_BINDING_MAX 12

typedef struct ProcessBinding {
    char *name;
    uuid_t iid;
    FacetHandle handle;
} ProcessBinding;

struct Dominit0ProcessEnvironment {
    IProcessEnvironment interface;
    FacetHandle handle;
    ProcessBinding bindings[PROCESS_BINDING_MAX];
    BindingInfo listed[PROCESS_BINDING_MAX];
    size_t binding_count;
    FacetHandle owned_session;
};

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static bool name_equal(const FacetString *left, const char *right)
{
    size_t length = strlen(right);
    return left != NULL && left->data != NULL && left->length == length &&
           memcmp(left->data, right, length) == 0;
}

static ProcessBinding *find_binding(Dominit0ProcessEnvironment *environment,
                                    const FacetString *name)
{
    if (environment == NULL || name == NULL || name->data == NULL ||
        name->length == 0 || name->length > 127)
        return NULL;
    for (size_t i = 0; i < environment->binding_count; i++)
        if (name_equal(name, environment->bindings[i].name))
            return &environment->bindings[i];
    return NULL;
}

static int bind(Dominit0ProcessEnvironment *environment, const char *name,
                uuid_t iid, FacetHandle handle)
{
    if (environment == NULL || name == NULL || name[0] == '\0' ||
        strlen(name) > 127 || handle.platform == NULL ||
        environment->binding_count == PROCESS_BINDING_MAX)
        return -1;
    FacetString candidate = {.data = name, .length = strlen(name)};
    if (find_binding(environment, &candidate) != NULL) return -1;
    char *copy = strdup(name);
    if (copy == NULL) return -1;
    ProcessBinding *binding = &environment->bindings[environment->binding_count++];
    binding->name = copy;
    binding->iid = iid;
    binding->handle = handle;
    return 0;
}

static FacetResult get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    Dominit0ProcessEnvironment *environment = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (!iid_equal(iid, IID_IGenericObject) &&
        !iid_equal(iid, IID_IProcessEnvironment))
        return FACET_NO_INTERFACE;
    *out = environment->handle;
    return FACET_OK;
}

static FacetResult resolve(void *self, const FacetString *name, FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    ProcessBinding *binding = find_binding(self, name);
    if (binding == NULL) return FACET_NOT_FOUND;
    return libfacet_handle_clone(binding->handle, out);
}

static FacetResult resolve_as(void *self, const FacetString *name, uuid_t iid,
                              FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    ProcessBinding *binding = find_binding(self, name);
    if (binding == NULL) return FACET_NOT_FOUND;
    if (!iid_equal(binding->iid, iid)) return FACET_NO_INTERFACE;
    return libfacet_handle_clone(binding->handle, out);
}

static FacetResult primary_iid(void *self, const FacetString *name, uuid_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    ProcessBinding *binding = find_binding(self, name);
    if (binding == NULL) return FACET_NOT_FOUND;
    *out = binding->iid;
    return FACET_OK;
}

static FacetResult advertised_iids(void *self, const FacetString *name,
                                   FacetArray_uuid *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    out->data = NULL;
    out->count = 0;
    ProcessBinding *binding = find_binding(self, name);
    if (binding == NULL) return FACET_NOT_FOUND;
    out->data = &binding->iid;
    out->count = 1;
    return FACET_OK;
}

static FacetResult list_bindings(void *self, FacetArray_BindingInfo *out)
{
    Dominit0ProcessEnvironment *environment = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    for (size_t i = 0; i < environment->binding_count; i++) {
        environment->listed[i].name.data = environment->bindings[i].name;
        environment->listed[i].name.length = strlen(environment->bindings[i].name);
        environment->listed[i].primary_iid = environment->bindings[i].iid;
    }
    out->data = environment->listed;
    out->count = environment->binding_count;
    return FACET_OK;
}

Dominit0ProcessEnvironment *dominit0_process_environment_create(
    Dominit0DomainEnvironment *parent, FacetHandle session)
{
    if (parent == NULL || session.platform == NULL) return NULL;
    Dominit0ProcessEnvironment *environment = calloc(1, sizeof(*environment));
    if (environment == NULL) return NULL;
    static const char *delegated[] = {
        "stdin", "stdout", "stderr", "terminal.control",
        "logger", "files", "auth", "security",
    };
    for (size_t i = 0; i < sizeof(delegated) / sizeof(delegated[0]); i++) {
        uuid_t iid;
        FacetHandle handle = {0};
        FacetResult result = dominit0_environment_resolve_named(
            parent, delegated[i], &iid, &handle);
        if (result == FACET_OK && bind(environment, delegated[i], iid, handle) != 0)
            goto fail;
    }
    if (libfacet_handle_clone(session, &environment->owned_session) != FACET_OK ||
        bind(environment, "session", IID_ISession,
             environment->owned_session) != 0)
        goto fail;
    environment->interface.self = environment;
    environment->interface.priv = environment;
    environment->interface.getInterface = get_interface;
    environment->interface.resolve = resolve;
    environment->interface.resolve_as = resolve_as;
    environment->interface.get_primary_iid = primary_iid;
    environment->interface.get_advertised_iids = advertised_iids;
    environment->interface.list_bindings = list_bindings;
    if (libfacet_export_interface(&environment->interface,
                                  &IProcessEnvironment_MetaData,
                                  &environment->handle) != FACET_OK)
        goto fail;
    return environment;
fail:
    dominit0_process_environment_destroy(environment);
    return NULL;
}

int dominit0_process_environment_bind_page_allocator(
    Dominit0ProcessEnvironment *environment, FacetHandle allocator)
{
    return bind(environment, "memory.pages", IID_IPageAllocator, allocator);
}

FacetHandle dominit0_process_environment_root_handle(
    const Dominit0ProcessEnvironment *environment)
{
    return environment == NULL ? (FacetHandle){0} : environment->handle;
}

void dominit0_process_environment_destroy(Dominit0ProcessEnvironment *environment)
{
    if (environment == NULL) return;
    if (environment->handle.platform != NULL)
        (void)libfacet_unexport_interface(environment->handle);
    if (environment->owned_session.platform != NULL)
        (void)libfacet_handle_release(environment->owned_session);
    for (size_t i = 0; i < environment->binding_count; i++)
        free(environment->bindings[i].name);
    free(environment);
}
