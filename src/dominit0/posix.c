#include <facetos/dominit0/posix.h>

#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/interfaces/IProcessLifecycle.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define POSIX_DESCRIPTOR_COUNT 64

typedef enum DescriptorKind {
    DESCRIPTOR_UNUSED = 0,
    DESCRIPTOR_INPUT,
    DESCRIPTOR_OUTPUT,
    DESCRIPTOR_FILE,
} DescriptorKind;

typedef struct PosixDescriptor {
    DescriptorKind kind;
    FacetHandle handle;
    uint64_t offset;
} PosixDescriptor;

struct Dominit0PosixView {
    IPOSIXView interface;
    FacetHandle handle;
    FacetHandle cwd_handle;
    FacetHandle page_allocator_handle;
    FacetHandle lifecycle_handle;
    PosixDescriptor descriptors[POSIX_DESCRIPTOR_COUNT];
};

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    Dominit0PosixView *view = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (!iid_equal(iid, IID_IGenericObject) && !iid_equal(iid, IID_IPOSIXView))
        return FACET_NO_INTERFACE;
    *out = view->handle;
    return FACET_OK;
}

static FacetResult posix_write(void *self, int32_t fd,
                               const FacetArray_u8 *data,
                               int64_t *result, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (data == NULL || result == NULL || error == NULL ||
        (data->count != 0 && data->data == NULL))
        return FACET_INVALID_ARGUMENT;
    *result = -1;
    *error = 0;
    if (fd < 0 || fd >= POSIX_DESCRIPTOR_COUNT ||
        view->descriptors[fd].kind == DESCRIPTOR_UNUSED) {
        *error = EBADF;
        return FACET_OK;
    }
    PosixDescriptor *descriptor = &view->descriptors[fd];
    if (descriptor->kind != DESCRIPTOR_OUTPUT) {
        *error = descriptor->kind == DESCRIPTOR_FILE ? EROFS : EBADF;
        return FACET_OK;
    }
    IByteWriter *writer = libfacet_proxy_from_handle(
        &IByteWriter_MetaData, descriptor->handle);
    if (writer == NULL) return FACET_INVALID_HANDLE;
    uint32_t written = 0;
    FacetResult transport = writer->write_bytes(writer->self, data, &written);
    libfacet_free_proxy_client(writer);
    if (transport == FACET_ACCESS_DENIED) {
        *error = EACCES;
        return FACET_OK;
    }
    if (transport != FACET_OK) return transport;
    *result = written;
    return FACET_OK;
}

static FacetResult posix_read(void *self, int32_t fd, uint32_t maximum,
                              FacetArray_u8 *data, int64_t *result,
                              int32_t *error)
{
    Dominit0PosixView *view = self;
    if (data == NULL || result == NULL || error == NULL)
        return FACET_INVALID_ARGUMENT;
    *data = (FacetArray_u8){0};
    *result = -1;
    *error = 0;
    if (fd < 0 || fd >= POSIX_DESCRIPTOR_COUNT ||
        view->descriptors[fd].kind == DESCRIPTOR_UNUSED) {
        *error = EBADF;
        return FACET_OK;
    }
    PosixDescriptor *descriptor = &view->descriptors[fd];
    FacetResult transport;
    if (descriptor->kind == DESCRIPTOR_INPUT) {
        IByteReader *reader = libfacet_proxy_from_handle(
            &IByteReader_MetaData, descriptor->handle);
        if (reader == NULL) return FACET_INVALID_HANDLE;
        transport = reader->read_bytes(reader->self, maximum, data);
        libfacet_free_proxy_client(reader);
    } else if (descriptor->kind == DESCRIPTOR_FILE) {
        IFile *file = libfacet_proxy_from_handle(&IFile_MetaData,
                                                  descriptor->handle);
        if (file == NULL) return FACET_INVALID_HANDLE;
        transport = file->read_at(file->self, descriptor->offset, maximum,
                                  data);
        libfacet_free_proxy_client(file);
        if (transport == FACET_OK) descriptor->offset += data->count;
    } else {
        *error = EBADF;
        return FACET_OK;
    }
    if (transport == FACET_ACCESS_DENIED) {
        *error = EACCES;
        return FACET_OK;
    }
    if (transport != FACET_OK) return transport;
    *result = (int64_t)data->count;
    return FACET_OK;
}

static FacetResult posix_open(void *self, const FacetString *path,
                              int32_t flags, uint32_t mode,
                              int32_t *fd, int32_t *error)
{
    (void)mode;
    Dominit0PosixView *view = self;
    if (path == NULL || path->data == NULL || path->length == 0 ||
        fd == NULL || error == NULL)
        return FACET_INVALID_ARGUMENT;
    *fd = -1;
    *error = 0;
    if ((flags & O_ACCMODE) != O_RDONLY ||
        (flags & (O_CREAT | O_TRUNC | O_APPEND)) != 0) {
        *error = EROFS;
        return FACET_OK;
    }
    int slot = -1;
    for (int i = 3; i < POSIX_DESCRIPTOR_COUNT; i++)
        if (view->descriptors[i].kind == DESCRIPTOR_UNUSED) { slot = i; break; }
    if (slot < 0) { *error = EMFILE; return FACET_OK; }
    FacetHandle cwd_copy = {0};
    if (libfacet_handle_clone(view->cwd_handle, &cwd_copy) != FACET_OK)
        return FACET_INVALID_HANDLE;
    IDirectory *cwd = libfacet_proxy_from_handle(&IDirectory_MetaData,
                                                  cwd_copy);
    FacetHandle file_handle = {0};
    FacetResult opened = cwd == NULL ? FACET_INVALID_HANDLE :
        cwd->open_file(cwd->self, path, &file_handle);
    libfacet_free_proxy_client(cwd);
    if (opened == FACET_NOT_FOUND) { *error = ENOENT; return FACET_OK; }
    if (opened == FACET_ACCESS_DENIED) { *error = EACCES; return FACET_OK; }
    if (opened != FACET_OK) return opened;
    view->descriptors[slot] = (PosixDescriptor){
        .kind = DESCRIPTOR_FILE, .handle = file_handle, .offset = 0,
    };
    *fd = slot;
    return FACET_OK;
}

static FacetResult posix_close(void *self, int32_t fd, int32_t *result,
                               int32_t *error)
{
    Dominit0PosixView *view = self;
    if (result == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *result = -1;
    *error = 0;
    if (fd < 0 || fd >= POSIX_DESCRIPTOR_COUNT ||
        view->descriptors[fd].kind == DESCRIPTOR_UNUSED) {
        *error = EBADF;
        return FACET_OK;
    }
    if (fd < 3) { *error = EBADF; return FACET_OK; }
    (void)libfacet_handle_release(view->descriptors[fd].handle);
    view->descriptors[fd] = (PosixDescriptor){0};
    *result = 0;
    return FACET_OK;
}

static FacetResult posix_seek(void *self, int32_t fd, int64_t offset,
                              int32_t whence, int64_t *result,
                              int32_t *error)
{
    Dominit0PosixView *view = self;
    if (result == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *result = -1;
    *error = 0;
    if (fd < 0 || fd >= POSIX_DESCRIPTOR_COUNT ||
        view->descriptors[fd].kind == DESCRIPTOR_UNUSED) {
        *error = EBADF;
        return FACET_OK;
    }
    PosixDescriptor *descriptor = &view->descriptors[fd];
    if (descriptor->kind != DESCRIPTOR_FILE) { *error = ESPIPE; return FACET_OK; }
    IFile *file = libfacet_proxy_from_handle(&IFile_MetaData,
                                              descriptor->handle);
    uint64_t size = 0;
    FacetResult transport = file == NULL ? FACET_INVALID_HANDLE :
        file->get_size(file->self, &size);
    libfacet_free_proxy_client(file);
    if (transport != FACET_OK) return transport;
    int64_t base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int64_t)descriptor->offset;
    else if (whence == SEEK_END) base = (int64_t)size;
    else { *error = EINVAL; return FACET_OK; }
    if ((offset < 0 && base < -offset) ||
        (offset > 0 && base > INT64_MAX - offset)) {
        *error = EINVAL;
        return FACET_OK;
    }
    descriptor->offset = (uint64_t)(base + offset);
    *result = (int64_t)descriptor->offset;
    return FACET_OK;
}

static FacetResult posix_allocate_pages(void *self, uint64_t count,
                                        void **pages)
{
    Dominit0PosixView *view = self;
    if (pages == NULL || view->page_allocator_handle.platform == NULL)
        return FACET_INVALID_HANDLE;
    IPageAllocator *allocator = libfacet_proxy_from_handle(
        &IPageAllocator_MetaData, view->page_allocator_handle);
    if (allocator == NULL) return FACET_INVALID_HANDLE;
    FacetResult result = allocator->alloc(allocator->self, count, pages);
    libfacet_free_proxy_client(allocator);
    return result;
}

static FacetResult posix_free_pages(void *self, uint64_t count, uint64_t base)
{
    Dominit0PosixView *view = self;
    if (view->page_allocator_handle.platform == NULL)
        return FACET_INVALID_HANDLE;
    IPageAllocator *allocator = libfacet_proxy_from_handle(
        &IPageAllocator_MetaData, view->page_allocator_handle);
    if (allocator == NULL) return FACET_INVALID_HANDLE;
    FacetResult result = allocator->free(allocator->self, count, base);
    libfacet_free_proxy_client(allocator);
    return result;
}

static FacetResult posix_exit(void *self, int32_t status)
{
    Dominit0PosixView *view = self;
    if (view->lifecycle_handle.platform == NULL) return FACET_INVALID_HANDLE;
    IProcessLifecycle *lifecycle = libfacet_proxy_from_handle(
        &IProcessLifecycle_MetaData, view->lifecycle_handle);
    if (lifecycle == NULL) return FACET_INVALID_HANDLE;
    FacetResult result = lifecycle->notify_exit(lifecycle->self, status);
    libfacet_free_proxy_client(lifecycle);
    return result;
}

Dominit0PosixView *dominit0_posix_view_create(
    FacetHandle stdin_handle, FacetHandle stdout_handle,
    FacetHandle files_handle, FacetHandle cwd_handle)
{
    (void)files_handle;
    if (stdin_handle.platform == NULL || stdout_handle.platform == NULL)
        return NULL;
    Dominit0PosixView *view = calloc(1, sizeof(*view));
    if (view == NULL) return NULL;
    if (cwd_handle.platform != NULL &&
        libfacet_handle_clone(cwd_handle, &view->cwd_handle) != FACET_OK) {
        free(view);
        return NULL;
    }
    view->descriptors[0] = (PosixDescriptor){DESCRIPTOR_INPUT, stdin_handle, 0};
    view->descriptors[1] = (PosixDescriptor){DESCRIPTOR_OUTPUT, stdout_handle, 0};
    view->descriptors[2] = (PosixDescriptor){DESCRIPTOR_OUTPUT, stdout_handle, 0};
    view->interface = (IPOSIXView){
        .self = view, .priv = view, .getInterface = get_interface,
        .write_fd = posix_write, .read_fd = posix_read,
        .open_fd = posix_open, .close_fd = posix_close,
        .seek_fd = posix_seek, .allocate_pages = posix_allocate_pages,
        .free_pages = posix_free_pages, .exit_process = posix_exit,
    };
    if (libfacet_export_interface(&view->interface, &IPOSIXView_MetaData,
                                  &view->handle) != FACET_OK) {
        (void)libfacet_handle_release(view->cwd_handle);
        free(view);
        return NULL;
    }
    return view;
}

int dominit0_posix_view_bind_page_allocator(Dominit0PosixView *view,
                                             FacetHandle allocator)
{
    if (view == NULL || allocator.platform == NULL ||
        view->page_allocator_handle.platform != NULL)
        return -1;
    view->page_allocator_handle = allocator;
    return 0;
}

int dominit0_posix_view_bind_lifecycle(Dominit0PosixView *view,
                                       FacetHandle lifecycle)
{
    if (view == NULL || lifecycle.platform == NULL ||
        view->lifecycle_handle.platform != NULL)
        return -1;
    view->lifecycle_handle = lifecycle;
    return 0;
}

FacetHandle dominit0_posix_view_handle(const Dominit0PosixView *view)
{
    return view == NULL ? (FacetHandle){0} : view->handle;
}

void dominit0_posix_view_destroy(Dominit0PosixView *view)
{
    if (view == NULL) return;
    if (view->handle.platform != NULL)
        (void)libfacet_unexport_interface(view->handle);
    for (int fd = 3; fd < POSIX_DESCRIPTOR_COUNT; fd++)
        if (view->descriptors[fd].kind != DESCRIPTOR_UNUSED)
            (void)libfacet_handle_release(view->descriptors[fd].handle);
    if (view->cwd_handle.platform != NULL)
        (void)libfacet_handle_release(view->cwd_handle);
    free(view);
}
