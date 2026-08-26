#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

ssize_t read(int fd, void *buffer, size_t count)
{
    if ((buffer == NULL && count != 0) || count > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }
    IPOSIXView *view = facet_posix_view();
    FacetArray_u8 data = {0};
    int64_t result = -1;
    int32_t error = 0;
    FacetResult transport = view == NULL ? FACET_INVALID_HANDLE :
        view->read_fd(view->self, fd, (uint32_t)count, &data, &result, &error);
    if (transport != FACET_OK) { errno = EIO; return -1; }
    if (result < 0) { errno = error == 0 ? EIO : error; return -1; }
    if ((uint64_t)result > count || data.count != (size_t)result) {
        free(data.data);
        errno = EIO;
        return -1;
    }
    memcpy(buffer, data.data, data.count);
    free(data.data);
    return (ssize_t)result;
}
