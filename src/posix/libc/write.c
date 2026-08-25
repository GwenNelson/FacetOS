#include <facet_posix_runtime.h>

#include <facetos/interfaces/IPOSIXView.h>

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

ssize_t write(int fd, const void *buffer, size_t count)
{
    IPOSIXView *view = facet_posix_view();
    if (view == NULL || (buffer == NULL && count != 0)) {
        errno = EINVAL;
        return -1;
    }
    FacetArray_u8 data = {
        .data = (uint8_t *)(uintptr_t)buffer,
        .count = count,
    };
    int64_t result = -1;
    int32_t error = 0;
    FacetResult transport = view->write_fd(view->self, fd, &data,
                                            &result, &error);
    if (transport != FACET_OK) {
        errno = EIO;
        return -1;
    }
    if (result < 0) {
        errno = error == 0 ? EIO : error;
        return -1;
    }
    return (ssize_t)result;
}
