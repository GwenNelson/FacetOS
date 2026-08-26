#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int stat(const char *restrict path, struct stat *restrict value)
{
    if (path == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    FacetString requested = {.data = path, .length = strlen(path)};
    uint32_t mode = 0, uid = 0, gid = 0;
    int32_t error = 0;
    IPOSIXView *view = facet_posix_view();
    FacetResult result = view == NULL ? FACET_INVALID_HANDLE :
        view->stat_path(view->self, &requested, &mode, &uid, &gid, &error);
    if (result != FACET_OK || error != 0) {
        errno = error == 0 ? EIO : error;
        return -1;
    }
    memset(value, 0, sizeof(*value));
    value->st_mode = (mode_t)mode;
    value->st_uid = (uid_t)uid;
    value->st_gid = (gid_t)gid;
    value->st_nlink = 1;
    value->st_blksize = 4096;
    return 0;
}

int lstat(const char *restrict path, struct stat *restrict value)
{
    return stat(path, value);
}
