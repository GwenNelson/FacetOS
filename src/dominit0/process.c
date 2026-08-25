#include <facetos/dominit0/process.h>

#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>
#include <facetos/initrd.h>
#include <facetos/interfaces/IProcess.h>
#include <facetos/interfaces/IProcessManager.h>
#include <facetos/interfaces/ISession.h>

#include <stdlib.h>
#include <string.h>

typedef struct RunningProcess RunningProcess;
typedef struct ProcessManager ProcessManager;

struct RunningProcess {
    IProcess interface;
    FacetHandle handle;
    void *platform_state;
    Dominit0ProcessEnvironment *environment;
    RunningProcess *next;
};

struct ProcessManager {
    IProcessManager interface;
    FacetHandle handle;
    CurrentDomain *domain;
    RunningProcess *processes;
    ProcessManager *next;
};

static ProcessManager *managers;

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
    *running = ((RunningProcess *)self)->platform_state != NULL;
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

static FacetResult launch_process(ProcessManager *manager,
                                  const FacetString *path,
                                  const FacetArray_string *arguments,
                                  FacetHandle session_handle,
                                  bool initial,
                                  FacetHandle *out)
{
    if (out == NULL || arguments == NULL || arguments->count > 64 ||
        (!initial && session_handle.platform == NULL))
        return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};

    char *path_copy = copy_string(path);
    if (path_copy == NULL) return FACET_INVALID_ARGUMENT;
    klog(LOG_DEBUG, "process manager: preparing %s%s\n", path_copy,
         initial ? " as initial process" : "");
    const uint8_t *elf_data;
    size_t elf_size;
    FacetResult result = facet_initrd_find_file(manager->domain->initrd,
                                                path_copy, &elf_data,
                                                &elf_size);
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
    process->environment = dominit0_process_environment_create(
        manager->domain->environment,
        initial ? (FacetHandle){0} : session_handle);
    if (process->environment == NULL) {
        free(process);
        result = FACET_OUT_OF_MEMORY;
        goto done;
    }
    klog(LOG_DEBUG, "process manager: environment ready for %s\n", path_copy);
    process->interface.self = process;
    process->interface.priv = process;
    process->interface.getInterface = process_get_interface;
    process->interface.getrunning = process_get_running;
    if (libfacet_export_interface(&process->interface, &IProcess_MetaData,
                                  &process->handle) != FACET_OK) {
        dominit0_process_environment_destroy(process->environment);
        free(process);
        result = FACET_OUT_OF_MEMORY;
        goto done;
    }
    process->platform_state = platform_start_process(
        manager->domain, elf_data, elf_size, (int)argc, argv,
        process->environment);
    if (process->platform_state == NULL) {
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

static FacetResult manager_launch(void *self, const FacetString *path,
                                  const FacetArray_string *arguments,
                                  FacetHandle session_handle,
                                  FacetHandle *out)
{
    ProcessManager *manager = self;
    FacetHandle owned = {0};
    if (libfacet_handle_clone(session_handle, &owned) != FACET_OK)
        return FACET_ACCESS_DENIED;
    ISession *session = libfacet_proxy_from_handle(&ISession_MetaData, owned);
    FacetHandle principal = {0};
    uint64_t session_domain_id = UINT64_MAX;
    uint64_t manager_domain_id = UINT64_MAX;
    FacetResult result = session == NULL ? FACET_ACCESS_DENIED :
        session->get_principal(session->self, &principal);
    if (result == FACET_OK)
        result = session->get_domain_id(session->self, &session_domain_id);
    if (result == FACET_OK)
        result = manager->domain->config->getdomain_id(
            manager->domain->config->self, &manager_domain_id);
    libfacet_free_proxy_client(session);
    if (principal.platform != NULL) (void)libfacet_handle_release(principal);
    if (result != FACET_OK || session_domain_id != manager_domain_id)
        return FACET_ACCESS_DENIED;
    return launch_process(self, path, arguments, session_handle, false, out);
}

static FacetResult manager_launch_initial(void *self, const FacetString *path,
                                          const FacetArray_string *arguments,
                                          FacetHandle *out)
{
    return launch_process(self, path, arguments, (FacetHandle){0}, true, out);
}

int dominit0_process_manager_initialize(CurrentDomain *domain)
{
    if (domain == NULL || domain->environment == NULL || domain->initrd == NULL)
        return -1;
    ProcessManager *manager = calloc(1, sizeof(*manager));
    if (manager == NULL) return -1;
    manager->domain = domain;
    manager->interface.self = manager;
    manager->interface.priv = manager;
    manager->interface.getInterface = manager_get_interface;
    manager->interface.launch = manager_launch;
    manager->interface.launch_initial = manager_launch_initial;
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
