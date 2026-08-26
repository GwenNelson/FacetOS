#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/posix.h>
#include <errno.h>
#include <string.h>

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
