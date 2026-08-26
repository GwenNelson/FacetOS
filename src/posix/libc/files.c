#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int open(const char *path, int flags, ...)
{
    if (path == NULL) { errno = EINVAL; return -1; }
    unsigned int mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, unsigned int);
        va_end(arguments);
    }
    FacetString value = {.data = path, .length = strlen(path)};
    int32_t fd = -1, error = 0;
    IPOSIXView *view = facet_posix_view();
    FacetResult transport = view == NULL ? FACET_INVALID_HANDLE :
        view->open_fd(view->self, &value, flags, mode, &fd, &error);
    if (transport != FACET_OK) { errno = EIO; return -1; }
    if (fd < 0) { errno = error == 0 ? EIO : error; return -1; }
    return fd;
}

int close(int fd)
{
    int32_t result = -1, error = 0;
    IPOSIXView *view = facet_posix_view();
    FacetResult transport = view == NULL ? FACET_INVALID_HANDLE :
        view->close_fd(view->self, fd, &result, &error);
    if (transport != FACET_OK) { errno = EIO; return -1; }
    if (result < 0) { errno = error == 0 ? EIO : error; return -1; }
    return result;
}

off_t lseek(int fd, off_t offset, int whence)
{
    int64_t result = -1;
    int32_t error = 0;
    IPOSIXView *view = facet_posix_view();
    FacetResult transport = view == NULL ? FACET_INVALID_HANDLE :
        view->seek_fd(view->self, fd, offset, whence, &result, &error);
    if (transport != FACET_OK) { errno = EIO; return (off_t)-1; }
    if (result < 0) { errno = error == 0 ? EIO : error; return (off_t)-1; }
    return (off_t)result;
}
