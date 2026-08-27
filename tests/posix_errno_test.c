#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static int path_error(const FacetString *path)
{
    if (path->length == strlen("/denied-directory") &&
        memcmp(path->data, "/denied-directory", path->length) == 0)
        return EACCES;
    if (path->length == strlen("/missing-directory") &&
        memcmp(path->data, "/missing-directory", path->length) == 0)
        return ENOENT;
    if (path->length == strlen("/missing-file") &&
        memcmp(path->data, "/missing-file", path->length) == 0)
        return ENOENT;
    return 0;
}

static FacetResult change_directory(void *self, const FacetString *path,
                                    int32_t *error)
{
    (void)self;
    *error = path_error(path);
    return FACET_OK;
}

static FacetResult list_directory(void *self, const FacetString *path,
                                  FacetArray_string *entries, int32_t *error)
{
    (void)self;
    *entries = (FacetArray_string){0};
    *error = path_error(path);
    return FACET_OK;
}

static FacetResult open_fd(void *self, const FacetString *path, int32_t flags,
                           uint32_t mode, int32_t *fd, int32_t *error)
{
    (void)self;
    (void)flags;
    (void)mode;
    *error = path_error(path);
    *fd = *error == 0 ? 3 : -1;
    return FACET_OK;
}

static FacetResult read_fd(void *self, int32_t fd, uint32_t maximum,
                           FacetArray_u8 *data, int64_t *result,
                           int32_t *error)
{
    (void)self;
    (void)fd;
    (void)maximum;
    *data = (FacetArray_u8){0};
    *result = -1;
    *error = EACCES;
    return FACET_OK;
}

static IPOSIXView view = {
    .change_directory = change_directory,
    .list_directory = list_directory,
    .open_fd = open_fd,
    .read_fd = read_fd,
};

IPOSIXView *facet_posix_view(void)
{
    return &view;
}

void facet_posix_yield(void)
{
}

void facet_rpc_release_value(FacetType type, const FacetTypeMeta *metadata,
                             void *value)
{
    (void)type;
    (void)metadata;
    (void)value;
}

int main(void)
{
    errno = 0;
    assert(chdir("/denied-directory") == -1 && errno == EACCES);
    errno = 0;
    assert(chdir("/missing-directory") == -1 && errno == ENOENT);

    errno = 0;
    assert(opendir("/denied-directory") == NULL && errno == EACCES);
    errno = 0;
    assert(opendir("/missing-directory") == NULL && errno == ENOENT);

    errno = 0;
    int fd = open("/unreadable-file", O_RDONLY);
    assert(fd == 3);
    char byte = 0;
    assert(read(fd, &byte, 1) == -1 && errno == EACCES);
    errno = 0;
    assert(open("/missing-file", O_RDONLY) == -1 && errno == ENOENT);
    return 0;
}
