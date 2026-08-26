#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/posix.h>
#include <errno.h>
#include <spawn.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

uint64_t get_domain_id(void)
{
    uint64_t value = 0;
    IPOSIXView *view = facet_posix_view();
    if (view == NULL || view->get_domain_id(view->self, &value) != FACET_OK)
        errno = EIO;
    return value;
}

pid_t getpid(void)
{
    int32_t value = -1;
    IPOSIXView *view = facet_posix_view();
    if (view == NULL || view->get_pid(view->self, &value) != FACET_OK) {
        errno = EIO;
        return -1;
    }
    return value;
}

int chdir(const char *path)
{
    FacetString value = {.data = path, .length = path == NULL ? 0 : strlen(path)};
    int32_t error = 0;
    IPOSIXView *view = facet_posix_view();
    FacetResult result = view == NULL ? FACET_INVALID_HANDLE :
        view->change_directory(view->self, &value, &error);
    if (result != FACET_OK || error != 0) { errno = error == 0 ? EIO : error; return -1; }
    return 0;
}

char *getcwd(char *buffer, size_t size)
{
    FacetString path = {0}; int32_t error = 0;
    IPOSIXView *view = facet_posix_view();
    FacetResult result = view == NULL ? FACET_INVALID_HANDLE :
        view->get_cwd(view->self, &path, &error);
    if (result != FACET_OK || error != 0 || path.data == NULL ||
        (buffer != NULL && size <= path.length)) { errno = error == 0 ? EIO : error; return NULL; }
    if (buffer != NULL) { memcpy(buffer, path.data, path.length); buffer[path.length] = 0; return buffer; }
    return (char *)(uintptr_t)path.data;
}

static int wait_view(IPOSIXView *view, pid_t pid, int *status)
{
    int32_t error = 0, result_status = 0;
    FacetResult result;
    while ((result = view->wait_process(view->self, pid, &result_status, &error)) == FACET_OK &&
           error != 0)
        facet_posix_yield();
    if (result != FACET_OK || error != 0) { errno = error == 0 ? EIO : error; return -1; }
    if (status != NULL) *status = result_status;
    return 0;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    IPOSIXView *view = facet_posix_view();
    if (options != 0) { errno = ENOTSUP; return -1; }
    if (view == NULL || wait_view(view, pid, status) != 0) return -1;
    return pid;
}

int posix_spawn(pid_t *restrict pid, const char *restrict path,
                const posix_spawn_file_actions_t *actions,
                const posix_spawnattr_t *restrict attributes,
                char *const *restrict argv, char *const *restrict environment)
{
    IPOSIXView *view = facet_posix_view();
    (void)environment;
    if (actions != NULL || attributes != NULL) return ENOTSUP;
    if (view == NULL || path == NULL || pid == NULL) return EINVAL;
    FacetString program = {.data = path, .length = strlen(path)};
    FacetString values[32]; size_t count = 0;
    if (argv != NULL) for (; argv[count] != NULL && count < 32; count++)
        values[count] = (FacetString){.data = argv[count], .length = strlen(argv[count])};
    if (count == 32) return E2BIG;
    if (count == 0) values[count++] = program;
    FacetArray_string arguments = {.data = values, .count = count};
    int32_t error = 0, child = -1;
    FacetResult result = view->spawn_inherited(view->self, &program, &arguments,
                                                 &child, &error);
    if (result != FACET_OK || error != 0) return error == 0 ? EIO : error;
    *pid = child;
    return 0;
}

int facet_posix_login_shell(const char *user, const char *password,
                            const char *shell, pid_t *pid)
{
    IPOSIXView *view = facet_posix_view();
    if (view == NULL || user == NULL || password == NULL || shell == NULL || pid == NULL) {
        errno = EINVAL; return -1;
    }
    FacetString name = {.data = user, .length = strlen(user)};
    FacetString secret = {.data = password, .length = strlen(password)};
    FacetHandle session = {0}; int32_t error = 0, child = -1;
    if (view->authenticate(view->self, &name, &secret, &session, &error) != FACET_OK || error != 0) {
        errno = error == 0 ? EACCES : error; return -1;
    }
    FacetString program = {.data = shell, .length = strlen(shell)};
    FacetArray_string arguments = {.data = &program, .count = 1};
    FacetResult result = view->spawn_process(view->self, &program, &arguments,
                                              session, &child, &error);
    (void)libfacet_handle_release(session);
    if (result != FACET_OK || error != 0) { errno = error == 0 ? EIO : error; return -1; }
    *pid = child;
    return 0;
}
