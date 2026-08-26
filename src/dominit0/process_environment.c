#include <facetos/dominit0/environment.h>

#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IProcessLifecycle.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/interfaces/ISession.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/ITerminalControl.h>
#include <facetos/dominit0/posix.h>
#include <facetos/utf8.h>

#include <stdlib.h>
#include <string.h>

#define PROCESS_BINDING_MAX 16

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
    FacetHandle private_page_allocator;
    Dominit0ProcessProfile profile;
    Dominit0PosixView *posix_view;
    FacetString *sysv_environment;
    char **owned_sysv_environment;
    size_t sysv_environment_count;
    size_t sysv_environment_capacity;
    FacetHandle owned_cwd;
    uint64_t terminal_index;
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
        name->length == 0 || name->length > 127 ||
        !facet_utf8_is_valid(name->data, name->length))
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
        !facet_utf8_is_valid(name, strlen(name)) ||
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
        !iid_equal(iid, IID_IProcessEnvironment)) {
        if (iid_equal(iid, IID_IPOSIXView) && environment->posix_view != NULL) {
            *out = dominit0_posix_view_handle(environment->posix_view);
            return FACET_OK;
        }
        return FACET_NO_INTERFACE;
    }
    *out = environment->handle;
    return FACET_OK;
}

static FacetResult resolve(void *self, const FacetString *name, FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (name == NULL || name->data == NULL || name->length == 0 ||
        name->length > 127 || !facet_utf8_is_valid(name->data, name->length))
        return FACET_INVALID_ARGUMENT;
    ProcessBinding *binding = find_binding(self, name);
    if (binding == NULL) return FACET_NOT_FOUND;
    return libfacet_handle_clone(binding->handle, out);
}

static FacetResult resolve_as(void *self, const FacetString *name, uuid_t iid,
                              FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (name == NULL || name->data == NULL || name->length == 0 ||
        name->length > 127 || !facet_utf8_is_valid(name->data, name->length))
        return FACET_INVALID_ARGUMENT;
    ProcessBinding *binding = find_binding(self, name);
    if (binding == NULL) return FACET_NOT_FOUND;
    if (!iid_equal(binding->iid, iid)) return FACET_NO_INTERFACE;
    return libfacet_handle_clone(binding->handle, out);
}

static FacetResult primary_iid(void *self, const FacetString *name, uuid_t *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    if (name == NULL || name->data == NULL || name->length == 0 ||
        name->length > 127 || !facet_utf8_is_valid(name->data, name->length))
        return FACET_INVALID_ARGUMENT;
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
    if (name == NULL || name->data == NULL || name->length == 0 ||
        name->length > 127 || !facet_utf8_is_valid(name->data, name->length))
        return FACET_INVALID_ARGUMENT;
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

static FacetResult get_sysv_environment(void *self, FacetArray_string *out)
{
    Dominit0ProcessEnvironment *environment = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    out->data = environment->sysv_environment;
    out->count = environment->sysv_environment_count;
    return FACET_OK;
}

static FacetResult get_cwd(void *self, FacetHandle *out)
{
    Dominit0ProcessEnvironment *environment = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    return environment->owned_cwd.platform == NULL ? FACET_INVALID_HANDLE :
        libfacet_handle_clone(environment->owned_cwd, out);
}

static FacetResult get_standard_streams(void *self, FacetHandle *input,
                                        FacetHandle *output,
                                        FacetHandle *error)
{
    Dominit0ProcessEnvironment *environment = self;
    if (input == NULL || output == NULL || error == NULL)
        return FACET_INVALID_ARGUMENT;
    *input = (FacetHandle){0};
    *output = (FacetHandle){0};
    *error = (FacetHandle){0};
    FacetString stdin_name = {.data = "stdin", .length = 5};
    FacetString stdout_name = {.data = "stdout", .length = 6};
    FacetString stderr_name = {.data = "stderr", .length = 6};
    ProcessBinding *stdin_binding = find_binding(environment, &stdin_name);
    ProcessBinding *stdout_binding = find_binding(environment, &stdout_name);
    ProcessBinding *stderr_binding = find_binding(environment, &stderr_name);
    if (stdin_binding == NULL || stdout_binding == NULL ||
        stderr_binding == NULL)
        return FACET_INVALID_HANDLE;
    if (libfacet_handle_clone(stdin_binding->handle, input) != FACET_OK ||
        libfacet_handle_clone(stdout_binding->handle, output) != FACET_OK ||
        libfacet_handle_clone(stderr_binding->handle, error) != FACET_OK) {
        if (input->platform != NULL) (void)libfacet_handle_release(*input);
        if (output->platform != NULL) (void)libfacet_handle_release(*output);
        if (error->platform != NULL) (void)libfacet_handle_release(*error);
        *input = (FacetHandle){0};
        *output = (FacetHandle){0};
        *error = (FacetHandle){0};
        return FACET_OUT_OF_MEMORY;
    }
    return FACET_OK;
}

static FacetResult get_terminal_index(void *self, uint64_t *terminal_index)
{
    if (terminal_index == NULL) return FACET_INVALID_ARGUMENT;
    *terminal_index = ((Dominit0ProcessEnvironment *)self)->terminal_index;
    return FACET_OK;
}

static int add_sysv_environment(Dominit0ProcessEnvironment *environment,
                                const char *value)
{
    if (environment == NULL || value == NULL || strchr(value, '=') == NULL ||
        environment->sysv_environment_count == 128)
        return -1;
    if (environment->sysv_environment_count ==
        environment->sysv_environment_capacity) {
        size_t capacity = environment->sysv_environment_capacity == 0 ? 8 :
            environment->sysv_environment_capacity * 2;
        FacetString *strings = realloc(environment->sysv_environment,
                                      capacity * sizeof(*strings));
        if (strings == NULL) return -1;
        char **owned = realloc(environment->owned_sysv_environment,
                               capacity * sizeof(*owned));
        if (owned == NULL) {
            environment->sysv_environment = strings;
            return -1;
        }
        environment->sysv_environment = strings;
        environment->owned_sysv_environment = owned;
        environment->sysv_environment_capacity = capacity;
    }
    size_t index = environment->sysv_environment_count;
    char *copy = strdup(value);
    if (copy == NULL) return -1;
    environment->owned_sysv_environment[index] = copy;
    environment->sysv_environment[index].data = copy;
    environment->sysv_environment[index].length = strlen(copy);
    environment->sysv_environment_count++;
    return 0;
}

Dominit0ProcessEnvironment *dominit0_process_environment_create(
    Dominit0DomainEnvironment *parent, FacetHandle session,
    bool bootstrap_authority, Dominit0ProcessProfile profile,
    const FacetArray_string *sysv_environment, FacetHandle cwd)
{
    if (parent == NULL) return NULL;
    Dominit0ProcessEnvironment *environment = calloc(1, sizeof(*environment));
    if (environment == NULL) return NULL;
    environment->profile = profile;
    if (sysv_environment != NULL) {
        if (sysv_environment->count > 128 ||
            (sysv_environment->count != 0 &&
             sysv_environment->data == NULL))
            goto fail;
        for (size_t i = 0; i < sysv_environment->count; i++) {
            const FacetString *value = &sysv_environment->data[i];
            if (value->data == NULL || value->length == 0 ||
                value->length > 4096 ||
                !facet_utf8_is_valid(value->data, value->length))
                goto fail;
            char *temporary = malloc(value->length + 1);
            if (temporary == NULL) goto fail;
            memcpy(temporary, value->data, value->length);
            temporary[value->length] = '\0';
            int added = add_sysv_environment(environment, temporary);
            free(temporary);
            if (added != 0) goto fail;
        }
    } else if (add_sysv_environment(environment, "PATH=/FacetOS") != 0 ||
               add_sysv_environment(environment, "PWD=/") != 0 ||
               add_sysv_environment(environment, "USER=root") != 0 ||
               add_sysv_environment(environment, "HOME=/root") != 0 ||
               add_sysv_environment(environment,
                                    "SHELL=/FacetOS/FacetShell") != 0) {
        goto fail;
    }
    static const char *delegated[] = {"logger", "files", "processes"};
    for (size_t i = 0; profile == DOMINIT0_PROCESS_NATIVE &&
         i < sizeof(delegated) / sizeof(delegated[0]); i++) {
        uuid_t iid;
        FacetHandle handle = {0};
        FacetResult result = dominit0_environment_resolve_named(
            parent, delegated[i], &iid, &handle);
        if (result == FACET_OK && bind(environment, delegated[i], iid, handle) != 0)
            goto fail;
    }
    static const char *bootstrap_delegated[] = {
        "auth", "security",
    };
    if (bootstrap_authority && profile == DOMINIT0_PROCESS_NATIVE) {
        for (size_t i = 0;
             i < sizeof(bootstrap_delegated) / sizeof(bootstrap_delegated[0]);
             i++) {
            uuid_t iid;
            FacetHandle handle = {0};
            FacetResult result = dominit0_environment_resolve_named(
                parent, bootstrap_delegated[i], &iid, &handle);
            if (result == FACET_OK &&
                bind(environment, bootstrap_delegated[i], iid, handle) != 0)
                goto fail;
        }
    }
    if (session.platform != NULL && profile == DOMINIT0_PROCESS_NATIVE) {
        if (libfacet_handle_clone(session, &environment->owned_session) != FACET_OK ||
            bind(environment, "session", IID_ISession,
                 environment->owned_session) != 0)
            goto fail;
    }
    if (cwd.platform != NULL) {
        if (libfacet_handle_clone(cwd, &environment->owned_cwd) != FACET_OK)
            goto fail;
    } else {
        FacetString files_name = {.data = "files", .length = 5};
        ProcessBinding *files_binding = find_binding(environment, &files_name);
        IFileStore *files = files_binding == NULL ? NULL :
            libfacet_proxy_from_handle(&IFileStore_MetaData,
                                       files_binding->handle);
        FacetString root = {.data = "/", .length = 1};
        if (files != NULL &&
            files->open_directory(files->self, &root,
                                  &environment->owned_cwd) != FACET_OK) {
            libfacet_free_proxy_client(files);
            goto fail;
        }
        libfacet_free_proxy_client(files);
    }
    if (environment->owned_cwd.platform != NULL &&
        bind(environment, "cwd", IID_IDirectory,
             environment->owned_cwd) != 0)
        goto fail;
    environment->interface.self = environment;
    environment->interface.priv = environment;
    environment->interface.getInterface = get_interface;
    environment->interface.resolve = resolve;
    environment->interface.resolve_as = resolve_as;
    environment->interface.get_primary_iid = primary_iid;
    environment->interface.get_advertised_iids = advertised_iids;
    environment->interface.list_bindings = list_bindings;
    environment->interface.get_sysv_environment = get_sysv_environment;
    environment->interface.get_cwd = get_cwd;
    environment->interface.get_standard_streams = get_standard_streams;
    environment->interface.get_terminal_index = get_terminal_index;
    if (libfacet_export_interface(&environment->interface,
                                  &IProcessEnvironment_MetaData,
                                  &environment->handle) != FACET_OK)
        goto fail;
    return environment;
fail:
    dominit0_process_environment_destroy(environment);
    return NULL;
}

int dominit0_process_environment_bind_named(
    Dominit0ProcessEnvironment *environment, const char *name,
    uuid_t primary_iid, FacetHandle object)
{
    if (environment == NULL ||
        environment->profile == DOMINIT0_PROCESS_PURE_POSIX)
        return -1;
    return bind(environment, name, primary_iid, object);
}

int dominit0_process_environment_bind_page_allocator(
    Dominit0ProcessEnvironment *environment, FacetHandle allocator)
{
    if (environment != NULL &&
        environment->profile == DOMINIT0_PROCESS_PURE_POSIX) {
        environment->private_page_allocator = allocator;
        return dominit0_posix_view_bind_page_allocator(
            environment->posix_view, allocator);
    }
    return bind(environment, "memory.pages", IID_IPageAllocator, allocator);
}

int dominit0_process_environment_bind_lifecycle(
    Dominit0ProcessEnvironment *environment, FacetHandle lifecycle)
{
    if (environment == NULL || lifecycle.platform == NULL)
        return -1;
    if (environment->profile == DOMINIT0_PROCESS_PURE_POSIX)
        return dominit0_posix_view_bind_lifecycle(environment->posix_view,
                                                  lifecycle);
    return bind(environment, "process.lifecycle", IID_IProcessLifecycle,
                lifecycle);
}

int dominit0_process_environment_bind_terminal(
    Dominit0ProcessEnvironment *environment, FacetHandle input,
    FacetHandle output, FacetHandle control, FacetHandle terminal)
{
    (void)terminal;
    if (environment == NULL) return -1;
    if (environment->profile == DOMINIT0_PROCESS_PURE_POSIX) {
        FacetString files_name = {.data = "files", .length = 5};
        ProcessBinding *files = find_binding(environment, &files_name);
        environment->posix_view = dominit0_posix_view_create(
            input, output, files == NULL ? (FacetHandle){0} : files->handle,
            environment->owned_cwd);
        if (environment->posix_view == NULL) return -1;
        return bind(environment, "posix", IID_IPOSIXView,
                    dominit0_posix_view_handle(environment->posix_view));
    }
    return bind(environment, "terminal.control", IID_ITerminalControl,
                control) ||
           bind(environment, "stdin", IID_IByteReader, input) ||
           bind(environment, "stdout", IID_IByteWriter, output) ||
           bind(environment, "stderr", IID_IByteWriter, output);
}

void dominit0_process_environment_set_terminal_index(
    Dominit0ProcessEnvironment *environment, uint64_t terminal_index)
{
    if (environment != NULL) environment->terminal_index = terminal_index;
}

FacetHandle dominit0_process_environment_root_handle(
    const Dominit0ProcessEnvironment *environment)
{
    return environment == NULL ? (FacetHandle){0} : environment->handle;
}

int dominit0_process_environment_get_sysv(
    const Dominit0ProcessEnvironment *environment, size_t *count,
    const char *const **values)
{
    if (environment == NULL || count == NULL || values == NULL)
        return -1;
    *count = environment->sysv_environment_count;
    *values = (const char *const *)environment->owned_sysv_environment;
    return 0;
}

void dominit0_process_environment_destroy(Dominit0ProcessEnvironment *environment)
{
    if (environment == NULL) return;
    if (environment->handle.platform != NULL)
        (void)libfacet_unexport_interface(environment->handle);
    if (environment->owned_session.platform != NULL)
        (void)libfacet_handle_release(environment->owned_session);
    if (environment->owned_cwd.platform != NULL)
        (void)libfacet_handle_release(environment->owned_cwd);
    dominit0_posix_view_destroy(environment->posix_view);
    for (size_t i = 0; i < environment->binding_count; i++)
        free(environment->bindings[i].name);
    for (size_t i = 0; i < environment->sysv_environment_count; i++)
        free(environment->owned_sysv_environment[i]);
    free(environment->owned_sysv_environment);
    free(environment->sysv_environment);
    free(environment);
}
