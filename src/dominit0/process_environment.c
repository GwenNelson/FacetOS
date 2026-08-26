#include <facetos/dominit0/environment.h>

#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IProcessLifecycle.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/interfaces/ISession.h>
#include <facetos/interfaces/IPrincipal.h>
#include <facetos/interfaces/IHumanUser.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/ITerminalControl.h>
#include <facetos/dominit0/posix.h>
#include <facetos/dominit0/file_view.h>
#include <facetos/dominit0/klog.h>
#include <facetos/utf8.h>

#include <stdlib.h>
#include <stdio.h>
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
    bool posix_cwd_synthetic_etc;
    FacetString *sysv_environment;
    char **owned_sysv_environment;
    size_t sysv_environment_count;
    size_t sysv_environment_capacity;
    FacetHandle owned_cwd;
    Dominit0CredentialFileStore *credential_files;
    uint32_t uid;
    uint32_t gid;
    bool admin;
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

static FacetResult set_cwd(void *self, FacetHandle directory)
{
    Dominit0ProcessEnvironment *environment = self;
    if (environment == NULL || directory.platform == NULL)
        return FACET_INVALID_ARGUMENT;
    FacetHandle replacement = {0};
    if (libfacet_handle_clone(directory, &replacement) != FACET_OK)
        return FACET_INVALID_HANDLE;
    FacetString cwd_name = {.data = "cwd", .length = 3};
    ProcessBinding *binding = find_binding(environment, &cwd_name);
    if (binding == NULL) {
        (void)libfacet_handle_release(replacement);
        return FACET_INVALID_HANDLE;
    }
    FacetHandle previous = environment->owned_cwd;
    environment->owned_cwd = replacement;
    binding->handle = replacement;
    if (previous.platform != NULL) (void)libfacet_handle_release(previous);
    return FACET_OK;
}

static int sync_posix_cwd(void *context, FacetHandle directory,
                          bool synthetic_etc)
{
    Dominit0ProcessEnvironment *environment = context;
    if (environment == NULL) return -1;
    if (!synthetic_etc && set_cwd(environment, directory) != FACET_OK)
        return -1;
    environment->posix_cwd_synthetic_etc = synthetic_etc;
    return 0;
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

static int copy_facet_string(const FacetString *input, char **out)
{
    if (input == NULL || out == NULL || input->data == NULL ||
        input->length == 0 || input->length > 4096)
        return -1;
    char *copy = malloc(input->length + 1);
    if (copy == NULL) return -1;
    memcpy(copy, input->data, input->length);
    copy[input->length] = '\0';
    *out = copy;
    return 0;
}

static int session_identity(FacetHandle session_handle, uint32_t *uid,
                            uint32_t *gid, bool *admin, char **name,
                            char **home, char **shell)
{
    *uid = 0;
    *gid = 0;
    *admin = true;
    *name = strdup("root");
    *home = strdup("/root");
    *shell = strdup("/FacetOS/FacetShell");
    if (*name == NULL || *home == NULL || *shell == NULL) goto fail;
    if (session_handle.platform == NULL) return 0;

    FacetHandle session_copy = {0};
    if (libfacet_handle_clone(session_handle, &session_copy) != FACET_OK)
        goto fail;
    ISession *session = libfacet_proxy_from_handle(&ISession_MetaData,
                                                   session_copy);
    FacetHandle principal_handle = {0};
    FacetResult result = session == NULL ? FACET_INVALID_HANDLE :
        session->get_credentials(session->self, uid, gid, admin);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process environment: session credentials failed (%d)\n",
             (int)result);
    if (result == FACET_OK)
        result = session->get_principal(session->self, &principal_handle);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process environment: session principal failed (%d)\n",
             (int)result);
    libfacet_free_proxy_client(session);
    if (result != FACET_OK) goto fail;

    IPrincipal *principal = libfacet_proxy_from_handle(&IPrincipal_MetaData,
                                                       principal_handle);
    IHumanUser *human = principal == NULL ? NULL :
        libfacet_proxy_client_get_interface(principal, IID_IHumanUser);
    FacetString remote_name = {0}, remote_home = {0}, remote_shell = {0};
    result = principal == NULL ? FACET_INVALID_HANDLE :
        principal->getname(principal->self, &remote_name);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process environment: principal name failed (%d)\n",
             (int)result);
    if (result == FACET_OK)
        result = human == NULL ? FACET_NO_INTERFACE :
            human->gethome_path(human->self, &remote_home);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process environment: user home failed (%d, human=%s)\n",
             (int)result, human == NULL ? "null" : "present");
    if (result == FACET_OK)
        result = human->getdefault_shell(human->self, &remote_shell);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process environment: user shell failed (%d)\n",
             (int)result);
    char *new_name = NULL, *new_home = NULL, *new_shell = NULL;
    if (result == FACET_OK &&
        (copy_facet_string(&remote_name, &new_name) != 0 ||
         copy_facet_string(&remote_home, &new_home) != 0 ||
         copy_facet_string(&remote_shell, &new_shell) != 0))
        result = FACET_OUT_OF_MEMORY;
    free((void *)(uintptr_t)remote_name.data);
    free((void *)(uintptr_t)remote_home.data);
    free((void *)(uintptr_t)remote_shell.data);
    libfacet_free_proxy_client(human);
    libfacet_free_proxy_client(principal);
    if (result != FACET_OK) {
        free(new_name);
        free(new_home);
        free(new_shell);
        goto fail;
    }
    free(*name);
    free(*home);
    free(*shell);
    *name = new_name;
    *home = new_home;
    *shell = new_shell;
    return 0;
fail:
    free(*name);
    free(*home);
    free(*shell);
    *name = NULL;
    *home = NULL;
    *shell = NULL;
    return -1;
}

Dominit0ProcessEnvironment *dominit0_process_environment_create(
    Dominit0DomainEnvironment *parent, FacetHandle session,
    bool bootstrap_authority, Dominit0ProcessProfile profile,
    const FacetArray_string *sysv_environment, FacetHandle cwd)
{
    if (parent == NULL) return NULL;
    const char *failure_stage = "allocate environment";
    Dominit0ProcessEnvironment *environment = calloc(1, sizeof(*environment));
    if (environment == NULL) return NULL;
    environment->profile = profile;
    char *user_name = NULL, *home_path = NULL, *shell_path = NULL;
    failure_stage = "read session identity";
    if (session_identity(session, &environment->uid, &environment->gid,
                         &environment->admin, &user_name, &home_path,
                         &shell_path) != 0)
        goto fail;
    failure_stage = "copy process environment";
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
    } else {
        char user_value[256], home_value[4352], shell_value[4352];
        int user_length = snprintf(user_value, sizeof(user_value), "USER=%s",
                                   user_name);
        int home_length = snprintf(home_value, sizeof(home_value), "HOME=%s",
                                   home_path);
        int shell_length = snprintf(shell_value, sizeof(shell_value),
                                    "SHELL=%s", shell_path);
        if (user_length < 0 || (size_t)user_length >= sizeof(user_value) ||
            home_length < 0 || (size_t)home_length >= sizeof(home_value) ||
            shell_length < 0 || (size_t)shell_length >= sizeof(shell_value) ||
            add_sysv_environment(environment, "PATH=/FacetOS") != 0 ||
            add_sysv_environment(environment, "PWD=/") != 0 ||
            add_sysv_environment(environment, user_value) != 0 ||
            add_sysv_environment(environment, home_value) != 0 ||
            add_sysv_environment(environment, shell_value) != 0)
            goto fail;
    }
    free(user_name);
    free(home_path);
    free(shell_path);
    user_name = home_path = shell_path = NULL;

    static const char *delegated[] = {"logger", "processes"};
    failure_stage = "delegate domain services";
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
        failure_stage = "delegate bootstrap authority";
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
    FacetInitrd *initrd = dominit0_environment_initrd(parent);
    failure_stage = "create credential file view";
    if (initrd != NULL) {
        environment->credential_files = dominit0_credential_file_store_create(
            initrd, environment->uid, environment->gid,
            environment->admin);
        if (environment->credential_files == NULL ||
            bind(environment, "files", IID_IFileStore,
                 dominit0_credential_file_store_handle(
                     environment->credential_files)) != 0)
            goto fail;
    }
    if (session.platform != NULL) {
        failure_stage = "retain session";
        if (libfacet_handle_clone(session, &environment->owned_session) != FACET_OK)
            goto fail;
        if (profile == DOMINIT0_PROCESS_NATIVE &&
            bind(environment, "session", IID_ISession,
                 environment->owned_session) != 0) goto fail;
    }
    if (cwd.platform != NULL) {
        failure_stage = "inherit current directory";
        if (libfacet_handle_clone(cwd, &environment->owned_cwd) != FACET_OK)
            goto fail;
    } else {
        failure_stage = "open root current directory";
        FacetString files_name = {.data = "files", .length = 5};
        ProcessBinding *files_binding = find_binding(environment, &files_name);
        FacetHandle files_handle = {0};
        IFileStore *files = files_binding == NULL ||
            libfacet_handle_clone(files_binding->handle, &files_handle) !=
                FACET_OK ? NULL :
            libfacet_proxy_from_handle(&IFileStore_MetaData, files_handle);
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
    failure_stage = "export process environment";
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
    environment->interface.set_cwd = set_cwd;
    environment->interface.get_standard_streams = get_standard_streams;
    environment->interface.get_terminal_index = get_terminal_index;
    if (libfacet_export_interface(&environment->interface,
                                  &IProcessEnvironment_MetaData,
                                  &environment->handle) != FACET_OK)
        goto fail;
    return environment;
fail:
    klog(LOG_ERROR, "process environment: failed to %s\n", failure_stage);
    free(user_name);
    free(home_path);
    free(shell_path);
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

int dominit0_process_environment_bind_posix_process_control(
    Dominit0ProcessEnvironment *environment, void *context, uint64_t domain_id, int32_t pid,
    FacetHandle default_session, Dominit0PosixSpawn spawn, Dominit0PosixWait wait)
{
    if (environment == NULL || environment->profile != DOMINIT0_PROCESS_PURE_POSIX)
        return -1;
    return dominit0_posix_view_bind_process_control(environment->posix_view,
                                                    context, domain_id, pid, default_session,
                                                    spawn, wait);
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
        if (dominit0_posix_view_bind_cwd_sync(environment->posix_view,
                environment, sync_posix_cwd,
                environment->posix_cwd_synthetic_etc) != 0)
            return -1;
        return bind(environment, "posix", IID_IPOSIXView,
                    dominit0_posix_view_handle(environment->posix_view));
    }
    return bind(environment, "terminal.control", IID_ITerminalControl,
                control) ||
           bind(environment, "stdin", IID_IByteReader, input) ||
           bind(environment, "stdout", IID_IByteWriter, output) ||
           bind(environment, "stderr", IID_IByteWriter, output);
}

int dominit0_process_environment_set_posix_root(
    Dominit0ProcessEnvironment *environment, const char *path)
{
    if (environment == NULL || path == NULL || path[0] != '/' ||
        environment->profile != DOMINIT0_PROCESS_PURE_POSIX)
        return -1;
    FacetString files_name = {.data = "files", .length = 5};
    ProcessBinding *files_binding = find_binding(environment, &files_name);
    FacetHandle files_handle = {0}, root = {0};
    if (files_binding == NULL ||
        libfacet_handle_clone(files_binding->handle, &files_handle) != FACET_OK)
        return -1;
    IFileStore *files = libfacet_proxy_from_handle(&IFileStore_MetaData,
                                                    files_handle);
    FacetString requested = {.data = path, .length = strlen(path)};
    FacetResult result = files == NULL ? FACET_INVALID_HANDLE :
        files->open_directory(files->self, &requested, &root);
    libfacet_free_proxy_client(files);
    if (result != FACET_OK) return -1;
    ProcessBinding *cwd = find_binding(environment,
        &(FacetString){.data = "cwd", .length = 3});
    if (cwd == NULL) { (void)libfacet_handle_release(root); return -1; }
    if (environment->owned_cwd.platform != NULL)
        (void)libfacet_handle_release(environment->owned_cwd);
    environment->owned_cwd = root;
    cwd->handle = root;
    return 0;
}

FacetHandle dominit0_process_environment_cwd_handle(
    const Dominit0ProcessEnvironment *environment)
{
    FacetHandle copy = {0};
    if (environment == NULL || environment->owned_cwd.platform == NULL ||
        libfacet_handle_clone(environment->owned_cwd, &copy) != FACET_OK)
        return (FacetHandle){0};
    return copy;
}

void dominit0_process_environment_set_posix_synthetic_cwd(
    Dominit0ProcessEnvironment *environment, bool synthetic_etc)
{
    if (environment == NULL) return;
    environment->posix_cwd_synthetic_etc = synthetic_etc;
    dominit0_posix_view_set_synthetic_cwd(environment->posix_view,
                                          synthetic_etc);
}

bool dominit0_process_environment_posix_synthetic_cwd(
    const Dominit0ProcessEnvironment *environment)
{
    return environment != NULL && environment->posix_cwd_synthetic_etc;
}

void dominit0_process_environment_set_terminal_index(
    Dominit0ProcessEnvironment *environment, uint64_t terminal_index)
{
    if (environment != NULL) environment->terminal_index = terminal_index;
}

int dominit0_process_environment_set_terminal_name(
    Dominit0ProcessEnvironment *environment, const char *terminal_name)
{
    if (environment == NULL || terminal_name == NULL || terminal_name[0] == '\0')
        return -1;
    size_t name_length = strlen("FACET_TERMINAL=");
    for (size_t i = 0; i < environment->sysv_environment_count; i++) {
        char *value = environment->owned_sysv_environment[i];
        if (strncmp(value, "FACET_TERMINAL=", name_length) != 0) continue;
        size_t length = name_length + strlen(terminal_name) + 1;
        char *replacement = malloc(length);
        if (replacement == NULL) return -1;
        snprintf(replacement, length, "FACET_TERMINAL=%s", terminal_name);
        free(value);
        environment->owned_sysv_environment[i] = replacement;
        environment->sysv_environment[i].data = replacement;
        environment->sysv_environment[i].length = length - 1;
        return 0;
    }
    size_t length = name_length + strlen(terminal_name) + 1;
    char *value = malloc(length);
    if (value == NULL) return -1;
    snprintf(value, length, "FACET_TERMINAL=%s", terminal_name);
    int result = add_sysv_environment(environment, value);
    free(value);
    return result;
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
    dominit0_credential_file_store_destroy(environment->credential_files);
    for (size_t i = 0; i < environment->binding_count; i++)
        free(environment->bindings[i].name);
    for (size_t i = 0; i < environment->sysv_environment_count; i++)
        free(environment->owned_sysv_environment[i]);
    free(environment->owned_sysv_environment);
    free(environment->sysv_environment);
    free(environment);
}
