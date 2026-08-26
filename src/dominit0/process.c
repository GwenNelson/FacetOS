#include <facetos/dominit0/process.h>

#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/auth.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/terminal.h>
#include <facetos/initrd.h>
#include <facetos/interfaces/IProcess.h>
#include <facetos/interfaces/IProcessLifecycle.h>
#include <facetos/interfaces/IProcessManager.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/ISession.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct RunningProcess RunningProcess;
typedef struct ProcessManager ProcessManager;

struct RunningProcess {
    IProcess interface;
    IProcessLifecycle lifecycle;
    FacetHandle handle;
    FacetHandle lifecycle_handle;
    void *platform_state;
    bool running;
    int32_t exit_status;
    int32_t pid;
    uint64_t terminal_index;
    ProcessManager *manager;
    Dominit0ProcessEnvironment *environment;
    RunningProcess *next;
};

struct ProcessManager {
    IProcessManager interface;
    FacetHandle handle;
    CurrentDomain *domain;
    RunningProcess *processes;
    int32_t next_pid;
    ProcessManager *next;
};

static ProcessManager *managers;
static FacetResult posix_spawn_process(void *, const FacetString *,
    const FacetArray_string *, FacetHandle, int32_t *, int32_t *);
static FacetResult posix_wait_process(void *, int32_t, int32_t *, int32_t *);

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult return_handle(FacetHandle handle, FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (handle.platform == NULL) return FACET_INVALID_HANDLE;
    *out = handle;
    return FACET_OK;
}

static FacetResult process_get_interface(void *self, uuid_t iid,
                                         FacetHandle *out)
{
    RunningProcess *process = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IProcess))
        return return_handle(process->handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult process_get_running(void *self, bool *running)
{
    if (running == NULL) return FACET_INVALID_ARGUMENT;
    *running = __atomic_load_n(&((RunningProcess *)self)->running,
                               __ATOMIC_ACQUIRE);
    return FACET_OK;
}

static FacetResult process_get_exit_status(void *self, int32_t *status)
{
    if (status == NULL) return FACET_INVALID_ARGUMENT;
    *status = ((RunningProcess *)self)->exit_status;
    return FACET_OK;
}

static FacetResult lifecycle_get_interface(void *self, uuid_t iid,
                                           FacetHandle *out)
{
    RunningProcess *process = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_IProcessLifecycle))
        return return_handle(process->lifecycle_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult lifecycle_notify_exit(void *self, int32_t status)
{
    RunningProcess *process = self;
    process->exit_status = status;
    __atomic_store_n(&process->running, false, __ATOMIC_RELEASE);
    return FACET_OK;
}

static FacetResult manager_get_interface(void *self, uuid_t iid,
                                         FacetHandle *out)
{
    ProcessManager *manager = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_IProcessManager))
        return return_handle(manager->handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static char *copy_string(const FacetString *input)
{
    if (input == NULL || input->data == NULL || input->length == 0 ||
        input->length > 4096)
        return NULL;
    char *copy = malloc(input->length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, input->data, input->length);
    copy[input->length] = '\0';
    return copy;
}

/* The native and POSIX application ABIs have distinct startup contracts.  The
 * initrd namespace makes that choice explicit: native FacetOS programs live
 * under /FacetOS, while POSIX programs live under /bin and /sbin. */
static bool is_posix_program(const char *path)
{
    return path != NULL &&
        (strncmp(path, "/bin/", sizeof("/bin/") - 1) == 0 ||
         strncmp(path, "/sbin/", sizeof("/sbin/") - 1) == 0);
}

static FacetResult session_credentials(FacetHandle session_handle,
                                       uint32_t *uid, uint32_t *gid,
                                       bool *admin)
{
    *uid = 0;
    *gid = 0;
    *admin = true;
    if (session_handle.platform == NULL) return FACET_OK;
    FacetHandle copy = {0};
    if (libfacet_handle_clone(session_handle, &copy) != FACET_OK)
        return FACET_INVALID_HANDLE;
    ISession *session = libfacet_proxy_from_handle(&ISession_MetaData, copy);
    FacetResult result = session == NULL ? FACET_INVALID_HANDLE :
        session->get_credentials(session->self, uid, gid, admin);
    libfacet_free_proxy_client(session);
    return result;
}

static FacetResult launch_process(ProcessManager *manager,
                                  const FacetString *path,
                                  const FacetArray_string *arguments,
                                  FacetHandle session_handle,
                                  bool initial,
                                  uint64_t terminal_index,
                                  const FacetArray_string *sysv_environment,
                                  FacetHandle cwd_handle,
                                  bool force_posix,
                                  FacetHandle *out)
{
    if (out == NULL || arguments == NULL || arguments->count > 64 ||
        (!initial && session_handle.platform == NULL))
        return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};

    uint32_t uid = 0, gid = 0;
    bool admin = true;
    FacetResult result = session_credentials(session_handle, &uid, &gid,
                                             &admin);
    if (result != FACET_OK) return FACET_ACCESS_DENIED;

    char *path_copy = copy_string(path);
    if (path_copy == NULL) return FACET_INVALID_ARGUMENT;
    if (cwd_handle.platform != NULL) {
        FacetHandle cwd_copy = {0};
        if (libfacet_handle_clone(cwd_handle, &cwd_copy) != FACET_OK) {
            free(path_copy);
            return FACET_INVALID_HANDLE;
        }
        IDirectory *cwd = libfacet_proxy_from_handle(&IDirectory_MetaData,
                                                      cwd_copy);
        FacetHandle file_handle = {0};
        FacetString requested = {.data = path_copy,
                                 .length = strlen(path_copy)};
        FacetResult opened = cwd == NULL ? FACET_INVALID_HANDLE :
            cwd->open_file(cwd->self, &requested, &file_handle);
        libfacet_free_proxy_client(cwd);
        if (opened != FACET_OK) {
            free(path_copy);
            return opened;
        }
        IFile *file = libfacet_proxy_from_handle(&IFile_MetaData, file_handle);
        FacetString canonical = {0};
        opened = file == NULL ? FACET_INVALID_HANDLE :
            file->getpath(file->self, &canonical);
        libfacet_free_proxy_client(file);
        if (opened != FACET_OK || canonical.data == NULL ||
            canonical.length == 0) {
            free((void *)(uintptr_t)canonical.data);
            free(path_copy);
            return opened == FACET_OK ? FACET_INVALID_ARGUMENT : opened;
        }
        char *resolved = copy_string(&canonical);
        free((void *)(uintptr_t)canonical.data);
        if (resolved == NULL) {
            free(path_copy);
            return FACET_OUT_OF_MEMORY;
        }
        free(path_copy);
        path_copy = resolved;
    }
    klog(LOG_DEBUG, "process manager: preparing %s%s\n", path_copy,
         initial ? " as initial process" : "");
    result = facet_initrd_check_execute(manager->domain->initrd, path_copy,
                                        uid, gid, admin);
    if (result != FACET_OK) {
        free(path_copy);
        return result;
    }
    const uint8_t *elf_data;
    size_t elf_size;
    result = facet_initrd_find_file(manager->domain->initrd, path_copy,
                                    &elf_data, &elf_size);
    if (result != FACET_OK) {
        free(path_copy);
        return result;
    }

    size_t argc = arguments->count == 0 ? 1 : arguments->count;
    char **argv = calloc(argc, sizeof(*argv));
    if (argv == NULL) {
        free(path_copy);
        return FACET_OUT_OF_MEMORY;
    }
    if (arguments->count == 0) {
        argv[0] = strdup(path_copy);
    } else {
        for (size_t i = 0; i < argc; i++) {
            argv[i] = copy_string(&arguments->data[i]);
            if (argv[i] == NULL) break;
        }
    }
    for (size_t i = 0; i < argc; i++) {
        if (argv[i] != NULL) continue;
        for (size_t j = 0; j < argc; j++) free(argv[j]);
        free(argv);
        free(path_copy);
        return FACET_OUT_OF_MEMORY;
    }

    RunningProcess *process = calloc(1, sizeof(*process));
    if (process == NULL) {
        result = FACET_OUT_OF_MEMORY;
        goto done;
    }
    Dominit0ProcessProfile profile = DOMINIT0_PROCESS_NATIVE;
    if (force_posix || is_posix_program(path_copy) ||
        (manager->domain->parsed != NULL &&
        (manager->domain->parsed->personality ==
             FACET_CONFIG_PERSONALITY_POSIX ||
         (initial && terminal_index < manager->domain->parsed->terminal_count &&
          manager->domain->parsed->terminals[terminal_index].view ==
              FACET_CONFIG_TERMINAL_VIEW_POSIX))))
        profile = DOMINIT0_PROCESS_PURE_POSIX;
    process->pid = manager->next_pid++;
    if (process->pid <= 0) process->pid = manager->next_pid = 1;
    process->terminal_index = terminal_index;
    process->manager = manager;
    process->environment = dominit0_process_environment_create(
        manager->domain->environment,
        session_handle, initial && session_handle.platform == NULL, profile,
        sysv_environment, cwd_handle);
    if (process->environment == NULL) {
        free(process);
        result = FACET_OUT_OF_MEMORY;
        goto done;
    }
    if (terminal_index > SIZE_MAX ||
        dominit0_terminal_bind_process_environment(
            manager->domain, (size_t)terminal_index,
            process->environment) != 0) {
        dominit0_process_environment_destroy(process->environment);
        free(process);
        result = FACET_NOT_FOUND;
        goto done;
    }
    if (profile == DOMINIT0_PROCESS_PURE_POSIX &&
        dominit0_process_environment_bind_posix_process_control(
            process->environment, process, manager->domain->parsed->id,
            process->pid, session_handle, posix_spawn_process,
            posix_wait_process) != 0) {
        dominit0_process_environment_destroy(process->environment);
        free(process); result = FACET_ERROR; goto done;
    }
    klog(LOG_DEBUG, "process manager: environment ready for %s\n", path_copy);
    process->interface.self = process;
    process->interface.priv = process;
    process->interface.getInterface = process_get_interface;
    process->interface.getrunning = process_get_running;
    process->interface.getexit_status = process_get_exit_status;
    process->lifecycle.self = process;
    process->lifecycle.priv = process;
    process->lifecycle.getInterface = lifecycle_get_interface;
    process->lifecycle.notify_exit = lifecycle_notify_exit;
    if (libfacet_export_interface(&process->interface, &IProcess_MetaData,
                                  &process->handle) != FACET_OK) {
        dominit0_process_environment_destroy(process->environment);
        free(process);
        result = FACET_OUT_OF_MEMORY;
        goto done;
    }
    if (libfacet_export_interface(&process->lifecycle,
                                  &IProcessLifecycle_MetaData,
                                  &process->lifecycle_handle) != FACET_OK ||
        dominit0_process_environment_bind_lifecycle(
            process->environment, process->lifecycle_handle) != 0) {
        if (process->lifecycle_handle.platform != NULL)
            (void)libfacet_unexport_interface(process->lifecycle_handle);
        (void)libfacet_unexport_interface(process->handle);
        dominit0_process_environment_destroy(process->environment);
        free(process);
        result = FACET_OUT_OF_MEMORY;
        goto done;
    }
    __atomic_store_n(&process->running, true, __ATOMIC_RELEASE);
    process->platform_state = platform_start_process(
        manager->domain, elf_data, elf_size, (int)argc, argv,
        process->environment);
    if (process->platform_state == NULL) {
        __atomic_store_n(&process->running, false, __ATOMIC_RELEASE);
        (void)libfacet_unexport_interface(process->lifecycle_handle);
        (void)libfacet_unexport_interface(process->handle);
        dominit0_process_environment_destroy(process->environment);
        free(process);
        result = FACET_ERROR;
        goto done;
    }
    klog(LOG_INFO, "Started process %s\n", path_copy);
    process->next = manager->processes;
    manager->processes = process;
    *out = process->handle;
    result = FACET_OK;

done:
    for (size_t i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    free(path_copy);
    return result;
}

static FacetResult posix_spawn_process(void *context, const FacetString *path,
    const FacetArray_string *arguments, FacetHandle session, int32_t *pid,
    int32_t *error)
{
    RunningProcess *parent = context;
    if (pid == NULL || error == NULL || parent == NULL) return FACET_INVALID_ARGUMENT;
    *pid = -1; *error = 0;
    FacetHandle process_handle = {0};
    FacetResult result = launch_process(parent->manager, path, arguments,
        session, false, parent->terminal_index, NULL, (FacetHandle){0}, true,
        &process_handle);
    if (result == FACET_OK) {
        *pid = parent->manager->processes == NULL ? -1 :
            parent->manager->processes->pid;
        if (process_handle.platform != NULL) (void)libfacet_handle_release(process_handle);
        return FACET_OK;
    }
    klog(LOG_ERROR, "POSIX spawn %.*s failed: %d\n", (int)path->length,
         path->data, (int)result);
    *error = result == FACET_ACCESS_DENIED ? EACCES :
             result == FACET_NOT_FOUND ? ENOENT : EIO;
    return FACET_OK;
}

static FacetResult posix_wait_process(void *context, int32_t pid,
                                      int32_t *status, int32_t *error)
{
    RunningProcess *parent = context;
    if (parent == NULL || status == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *status = -1; *error = 0;
    for (RunningProcess *item = parent->manager->processes; item != NULL;
         item = item->next) {
        if (item->pid != pid) continue;
        if (__atomic_load_n(&item->running, __ATOMIC_ACQUIRE)) {
            *error = EAGAIN;
            return FACET_OK;
        }
        *status = item->exit_status;
        return FACET_OK;
    }
    *error = ECHILD;
    return FACET_OK;
}

static FacetResult manager_launch(void *self, const FacetString *path,
                                  const FacetArray_string *arguments,
                                  FacetHandle environment_handle,
                                  FacetHandle *out)
{
    ProcessManager *manager = self;
    FacetHandle owned = {0};
    if (libfacet_handle_clone(environment_handle, &owned) != FACET_OK)
        return FACET_ACCESS_DENIED;
    IProcessEnvironment *caller = libfacet_proxy_from_handle(
        &IProcessEnvironment_MetaData, owned);
    FacetString session_name = {.data = "session", .length = 7};
    FacetHandle session_handle = {0};
    FacetHandle cwd_handle = {0};
    FacetArray_string sysv_environment = {0};
    uint64_t terminal_index = 0;
    FacetResult result = caller == NULL ? FACET_ACCESS_DENIED :
        caller->resolve(caller->self, &session_name, &session_handle);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process manager: caller session lookup failed (%d)\n",
             (int)result);
    if (result == FACET_OK)
        result = caller->get_sysv_environment(caller->self,
                                              &sysv_environment);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process manager: caller environment lookup failed (%d)\n",
             (int)result);
    if (result == FACET_OK)
        result = caller->get_terminal_index(caller->self, &terminal_index);
    if (result == FACET_OK)
        result = caller->get_cwd(caller->self, &cwd_handle);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process manager: caller terminal lookup failed (%d)\n",
             (int)result);
    libfacet_free_proxy_client(caller);
    if (result != FACET_OK) {
        facet_rpc_release_value(FACET_TYPE_ARRAY,
                                &FacetArray_string_TypeMeta,
                                &sysv_environment);
        if (session_handle.platform != NULL)
            (void)libfacet_handle_release(session_handle);
        if (cwd_handle.platform != NULL)
            (void)libfacet_handle_release(cwd_handle);
        return FACET_ACCESS_DENIED;
    }
    FacetHandle session_copy = {0};
    if (libfacet_handle_clone(session_handle, &session_copy) != FACET_OK) {
        facet_rpc_release_value(FACET_TYPE_ARRAY,
                                &FacetArray_string_TypeMeta,
                                &sysv_environment);
        (void)libfacet_handle_release(session_handle);
        (void)libfacet_handle_release(cwd_handle);
        return FACET_ACCESS_DENIED;
    }
    ISession *session = libfacet_proxy_from_handle(&ISession_MetaData,
                                                    session_copy);
    FacetHandle principal = {0};
    uint64_t session_domain_id = UINT64_MAX;
    uint64_t manager_domain_id = UINT64_MAX;
    result = session == NULL ? FACET_ACCESS_DENIED :
        session->get_principal(session->self, &principal);
    if (result == FACET_OK)
        result = session->get_domain_id(session->self, &session_domain_id);
    if (result == FACET_OK)
        result = manager->domain->config->getdomain_id(
            manager->domain->config->self, &manager_domain_id);
    libfacet_free_proxy_client(session);
    if (principal.platform != NULL) (void)libfacet_handle_release(principal);
    if (result != FACET_OK || session_domain_id != manager_domain_id) {
        klog(LOG_ERROR,
             "process manager: caller domain validation failed (%d, %llu != %llu)\n",
             (int)result, (unsigned long long)session_domain_id,
             (unsigned long long)manager_domain_id);
        facet_rpc_release_value(FACET_TYPE_ARRAY,
                                &FacetArray_string_TypeMeta,
                                &sysv_environment);
        (void)libfacet_handle_release(session_handle);
        (void)libfacet_handle_release(cwd_handle);
        return FACET_ACCESS_DENIED;
    }
    result = launch_process(self, path, arguments, session_handle, false,
                            terminal_index, &sysv_environment, cwd_handle, false, out);
    if (result != FACET_OK)
        klog(LOG_ERROR, "process manager: child launch failed (%d)\n",
             (int)result);
    facet_rpc_release_value(FACET_TYPE_ARRAY,
                            &FacetArray_string_TypeMeta,
                            &sysv_environment);
    (void)libfacet_handle_release(session_handle);
    (void)libfacet_handle_release(cwd_handle);
    return result;
}

static FacetResult manager_launch_initial(void *self, const FacetString *path,
                                          const FacetArray_string *arguments,
                                          uint64_t terminal_index,
                                          FacetHandle *out)
{
    ProcessManager *manager = self;
    FacetHandle session = {0};
    if (manager->domain->parsed != NULL &&
        terminal_index < manager->domain->parsed->terminal_count) {
        const char *run_as =
            manager->domain->parsed->terminals[terminal_index].run_as;
        if (run_as != NULL) {
            FacetResult result = dominit0_auth_session_for_user(
                manager->domain->parsed->id, run_as, &session);
            if (result != FACET_OK) return result;
        }
    }
    if (session.platform == NULL && manager->domain->parsed != NULL &&
        manager->domain->parsed->personality == FACET_CONFIG_PERSONALITY_POSIX)
        (void)dominit0_auth_session_for_user(manager->domain->parsed->id, "root", &session);
    FacetResult result = launch_process(self, path, arguments, session, true,
                                        terminal_index, NULL,
                                        (FacetHandle){0}, false, out);
    if (session.platform != NULL) (void)libfacet_handle_release(session);
    return result;
}

static FacetResult manager_launch_on_terminal(
    void *self, const FacetString *path, const FacetArray_string *arguments,
    FacetHandle session_handle, uint64_t terminal_index, FacetHandle *out)
{
    ProcessManager *manager = self;
    FacetHandle owned = {0};
    if (libfacet_handle_clone(session_handle, &owned) != FACET_OK)
        return FACET_ACCESS_DENIED;
    ISession *session = libfacet_proxy_from_handle(&ISession_MetaData, owned);
    uint64_t session_domain_id = UINT64_MAX;
    uint64_t manager_domain_id = UINT64_MAX;
    FacetResult result = session == NULL ? FACET_ACCESS_DENIED :
        session->get_domain_id(session->self, &session_domain_id);
    if (result == FACET_OK)
        result = manager->domain->config->getdomain_id(
            manager->domain->config->self, &manager_domain_id);
    libfacet_free_proxy_client(session);
    if (result != FACET_OK || session_domain_id != manager_domain_id)
        return FACET_ACCESS_DENIED;
    return launch_process(manager, path, arguments, session_handle, false,
                          terminal_index, NULL, (FacetHandle){0}, false, out);
}

int dominit0_process_manager_initialize(CurrentDomain *domain)
{
    if (domain == NULL || domain->environment == NULL || domain->initrd == NULL)
        return -1;
    ProcessManager *manager = calloc(1, sizeof(*manager));
    if (manager == NULL) return -1;
    manager->domain = domain;
    manager->next_pid = 1;
    manager->interface.self = manager;
    manager->interface.priv = manager;
    manager->interface.getInterface = manager_get_interface;
    manager->interface.launch = manager_launch;
    manager->interface.launch_initial = manager_launch_initial;
    manager->interface.launch_on_terminal = manager_launch_on_terminal;
    if (libfacet_export_interface(&manager->interface,
                                  &IProcessManager_MetaData,
                                  &manager->handle) != FACET_OK ||
        dominit0_environment_bind_named(domain->environment, "processes",
                                        IID_IProcessManager,
                                        manager->handle) != 0) {
        if (manager->handle.platform != NULL)
            (void)libfacet_unexport_interface(manager->handle);
        free(manager);
        return -1;
    }
    manager->next = managers;
    managers = manager;
    return 0;
}

void dominit0_process_managers_destroy(void)
{
    while (managers != NULL) {
        ProcessManager *manager = managers;
        managers = manager->next;
        while (manager->processes != NULL) {
            RunningProcess *process = manager->processes;
            manager->processes = process->next;
            if (process->lifecycle_handle.platform != NULL)
                (void)libfacet_unexport_interface(process->lifecycle_handle);
            if (process->handle.platform != NULL)
                (void)libfacet_unexport_interface(process->handle);
            dominit0_process_environment_destroy(process->environment);
            free(process);
        }
        if (manager->handle.platform != NULL)
            (void)libfacet_unexport_interface(manager->handle);
        free(manager);
    }
}
