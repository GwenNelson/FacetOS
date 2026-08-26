#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* musl intentionally keeps DIR opaque in its public header.  libc-posix owns
 * the implementation here, so use the matching private storage shape. */
struct __dirstream {
    off_t tell;
    int fd;
    int buf_pos;
    int buf_end;
    volatile int lock[1];
    char buf[2048];
};

/* musl exposes DIR's storage layout, so retain our per-stream state in its
 * private buffer rather than changing a public libc definition. */
struct facet_dir_state {
    FacetArray_string entries;
    size_t next;
};

static struct facet_dir_state *state(DIR *directory)
{
    return (struct facet_dir_state *)(uintptr_t)directory->tell;
}

DIR *opendir(const char *path)
{
    if (path == NULL) { errno = EINVAL; return NULL; }
    IPOSIXView *view = facet_posix_view();
    FacetString value = {.data = path, .length = strlen(path)};
    int32_t error = 0;
    struct facet_dir_state *contents = calloc(1, sizeof(*contents));
    DIR *directory = calloc(1, sizeof(*directory));
    if (view == NULL || contents == NULL || directory == NULL ||
        view->list_directory(view->self, &value, &contents->entries, &error) != FACET_OK ||
        error != 0) {
        free(contents);
        free(directory);
        errno = error == 0 ? EIO : error;
        return NULL;
    }
    directory->tell = (off_t)(uintptr_t)contents;
    return directory;
}

struct dirent *readdir(DIR *directory)
{
    struct facet_dir_state *contents = directory == NULL ? NULL : state(directory);
    if (contents == NULL || contents->next == contents->entries.count) return NULL;
    FacetString name = contents->entries.data[contents->next++];
    if (name.length >= sizeof(((struct dirent *)0)->d_name)) { errno = EIO; return NULL; }
    struct dirent *entry = (struct dirent *)directory->buf;
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->d_name, name.data, name.length);
    entry->d_reclen = sizeof(*entry);
    return entry;
}

int closedir(DIR *directory)
{
    if (directory == NULL) { errno = EINVAL; return -1; }
    struct facet_dir_state *contents = state(directory);
    if (contents != NULL) {
        facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_string_TypeMeta,
                                &contents->entries);
        free(contents);
    }
    free(directory);
    return 0;
}
