#include <facetos/dominit0/posix.h>
#include <facetos/dominit0/auth.h>

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

extern FacetResult dominit0_authenticate_user(uint64_t, const FacetString *,
    const FacetString *, FacetHandle *) __attribute__((weak));

#define POSIX_DESCRIPTOR_COUNT 64

typedef enum DescriptorKind {
    DESCRIPTOR_UNUSED = 0,
    DESCRIPTOR_INPUT,
    DESCRIPTOR_OUTPUT,
    DESCRIPTOR_FILE,
    DESCRIPTOR_MEMORY,
} DescriptorKind;

typedef struct PosixDescriptor {
    DescriptorKind kind;
    FacetHandle handle;
    uint64_t offset;
    const uint8_t *memory;
    size_t memory_size;
} PosixDescriptor;

static const char virtual_passwd[] =
    "root:x:0:0:root:/root:/bin/sh\n"
    "user:x:1000:1000:user:/home/user:/bin/sh\n";
static const char virtual_shadow[] =
    "root:f490b96d6a372fd2fd1ab87bbe272a193567d04d23f5783862a187b201273f59:::::::\n"
    "user:f490b96d6a372fd2fd1ab87bbe272a193567d04d23f5783862a187b201273f59:::::::\n";
static const char virtual_fstab[] = "initrd / initrd ro 0 0\n";

struct Dominit0PosixView {
    IPOSIXView interface;
    FacetHandle handle;
    FacetHandle cwd_handle;
    FacetHandle page_allocator_handle;
    FacetHandle lifecycle_handle;
    uint64_t domain_id;
    int32_t pid;
    void *process_context;
    FacetHandle default_session;
    Dominit0PosixSpawn spawn;
    Dominit0PosixWait wait;
    PosixDescriptor descriptors[POSIX_DESCRIPTOR_COUNT];
};

static void *proxy_from_borrowed(const FacetInterfaceMeta *metadata,
                                 FacetHandle handle);

static FacetResult posix_get_domain_id(void *self, uint64_t *domain_id)
{
    if (domain_id == NULL) return FACET_INVALID_ARGUMENT;
    *domain_id = ((Dominit0PosixView *)self)->domain_id;
    return FACET_OK;
}

static FacetResult posix_get_pid(void *self, int32_t *pid)
{
    if (pid == NULL) return FACET_INVALID_ARGUMENT;
    *pid = ((Dominit0PosixView *)self)->pid;
    return FACET_OK;
}

static FacetResult posix_get_cwd(void *self, FacetString *path, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (path == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *path = (FacetString){0}; *error = 0;
    IDirectory *cwd = proxy_from_borrowed(&IDirectory_MetaData, view->cwd_handle);
    FacetResult result = cwd == NULL ? FACET_INVALID_HANDLE :
        cwd->getpath(cwd->self, path);
    libfacet_free_proxy_client(cwd);
    if (result == FACET_ACCESS_DENIED) { *error = EACCES; return FACET_OK; }
    return result;
}

static FacetResult posix_change_directory(void *self, const FacetString *path,
                                          int32_t *error)
{
    Dominit0PosixView *view = self;
    if (path == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *error = 0;
    IDirectory *cwd = proxy_from_borrowed(&IDirectory_MetaData, view->cwd_handle);
    FacetHandle next = {0};
    FacetResult result = cwd == NULL ? FACET_INVALID_HANDLE :
        cwd->open_directory(cwd->self, path, &next);
    libfacet_free_proxy_client(cwd);
    if (result == FACET_NOT_FOUND) { *error = ENOENT; return FACET_OK; }
    if (result == FACET_ACCESS_DENIED) { *error = EACCES; return FACET_OK; }
    if (result != FACET_OK) return result;
    if (view->cwd_handle.platform != NULL) (void)libfacet_handle_release(view->cwd_handle);
    view->cwd_handle = next;
    return FACET_OK;
}

static FacetResult posix_authenticate(void *self, const FacetString *u,
    const FacetString *p, FacetHandle *session, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (session == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *session = (FacetHandle){0}; *error = 0;
    if (dominit0_authenticate_user == NULL) { *error = ENOSYS; return FACET_OK; }
    FacetResult result = dominit0_authenticate_user(view->domain_id, u, p, session);
    if (result == FACET_ACCESS_DENIED) { *error = EACCES; return FACET_OK; }
    return result;
}
static FacetResult posix_spawn(void *self, const FacetString *p,
    const FacetArray_string *a, FacetHandle s, int32_t *pid, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (pid == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *pid = -1; *error = 0;
    if (view->spawn == NULL) { *error = ENOSYS; return FACET_OK; }
    if (s.platform == NULL) s = view->default_session;
    return view->spawn(view->process_context, p, a, s, pid, error);
}

static FacetResult posix_spawn_inherited(void *self, const FacetString *p,
    const FacetArray_string *a, int32_t *pid, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (pid == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *pid = -1;
    *error = 0;
    if (view->spawn == NULL || view->default_session.platform == NULL) {
        *error = ENOSYS;
        return FACET_OK;
    }
    return view->spawn(view->process_context, p, a, view->default_session,
                       pid, error);
}
static FacetResult posix_wait(void *self, int32_t pid, int32_t *status, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (status == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *status = -1; *error = 0;
    return view->wait == NULL ? (*error = ENOSYS, FACET_OK) :
        view->wait(view->process_context, pid, status, error);
}

static FacetResult posix_list_directory(void *self, const FacetString *path,
    FacetArray_string *entries, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (path == NULL || entries == NULL || error == NULL) return FACET_INVALID_ARGUMENT;
    *entries = (FacetArray_string){0}; *error = 0;
    IDirectory *cwd = proxy_from_borrowed(&IDirectory_MetaData, view->cwd_handle);
    FacetHandle handle = {0};
    FacetResult result = cwd == NULL ? FACET_INVALID_HANDLE :
        cwd->open_directory(cwd->self, path, &handle);
    libfacet_free_proxy_client(cwd);
    if (result == FACET_NOT_FOUND) { *error = ENOENT; return FACET_OK; }
    if (result == FACET_ACCESS_DENIED) { *error = EACCES; return FACET_OK; }
    if (result != FACET_OK) return result;
    IDirectory *directory = libfacet_proxy_from_handle(&IDirectory_MetaData, handle);
    FacetArray_Entry raw = {0}; uint64_t next = 0; bool end = false;
    result = directory == NULL ? FACET_INVALID_HANDLE :
        directory->list(directory->self, 0, 128, &raw, &next, &end);
    libfacet_free_proxy_client(directory);
    if (result != FACET_OK) return result;
    size_t count = raw.count;
    FacetString *copy = calloc(count, sizeof(*copy));
    if (count != 0 && copy == NULL) { facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_Entry_TypeMeta, &raw); return FACET_OUT_OF_MEMORY; }
    for (size_t i = 0; i < count; i++) {
        copy[i].data = strdup(raw.data[i].name.data);
        copy[i].length = raw.data[i].name.length;
        if (copy[i].data == NULL) { for (size_t j=0;j<i;j++) free((void*)copy[j].data); free(copy); facet_rpc_release_value(FACET_TYPE_ARRAY,&FacetArray_Entry_TypeMeta,&raw); return FACET_OUT_OF_MEMORY; }
    }
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_Entry_TypeMeta, &raw);
    entries->data = copy; entries->count = count;
    return FACET_OK;
}

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static void *proxy_from_borrowed(const FacetInterfaceMeta *metadata,
                                 FacetHandle handle)
{
    FacetHandle copy = {0};
    if (handle.platform == NULL ||
        libfacet_handle_clone(handle, &copy) != FACET_OK)
        return NULL;
    void *proxy = libfacet_proxy_from_handle(metadata, copy);
    if (proxy == NULL) (void)libfacet_handle_release(copy);
    return proxy;
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
    IByteWriter *writer = proxy_from_borrowed(
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
        IByteReader *reader = proxy_from_borrowed(
            &IByteReader_MetaData, descriptor->handle);
        if (reader == NULL) return FACET_INVALID_HANDLE;
        transport = reader->read_bytes(reader->self, maximum, data);
        libfacet_free_proxy_client(reader);
    } else if (descriptor->kind == DESCRIPTOR_MEMORY) {
        uint64_t available = descriptor->offset < descriptor->memory_size ?
            descriptor->memory_size - descriptor->offset : 0;
        uint32_t count = available < maximum ? (uint32_t)available : maximum;
        if (count != 0) {
            data->data = malloc(count);
            if (data->data == NULL) return FACET_OUT_OF_MEMORY;
            memcpy(data->data, descriptor->memory + descriptor->offset, count);
        }
        data->count = count;
        descriptor->offset += count;
        transport = FACET_OK;
    } else if (descriptor->kind == DESCRIPTOR_FILE) {
        IFile *file = proxy_from_borrowed(&IFile_MetaData,
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
    if (view->domain_id == 0) {
        const char *memory = NULL;
        if (path->length == sizeof("/etc/passwd") - 1 &&
            memcmp(path->data, "/etc/passwd", path->length) == 0) memory = virtual_passwd;
        else if (path->length == sizeof("/etc/shadow") - 1 &&
                 memcmp(path->data, "/etc/shadow", path->length) == 0) memory = virtual_shadow;
        else if (path->length == sizeof("/etc/fstab") - 1 &&
                 memcmp(path->data, "/etc/fstab", path->length) == 0) memory = virtual_fstab;
        if (memory != NULL) {
            view->descriptors[slot] = (PosixDescriptor){
                .kind = DESCRIPTOR_MEMORY, .memory = (const uint8_t *)memory,
                .memory_size = strlen(memory)};
            *fd = slot;
            return FACET_OK;
        }
    }
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
    if (descriptor->kind == DESCRIPTOR_MEMORY) {
        int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ?
            (int64_t)descriptor->offset : whence == SEEK_END ?
            (int64_t)descriptor->memory_size : -1;
        if (base < 0 || offset < -base || base + offset < 0) { *error = EINVAL; return FACET_OK; }
        descriptor->offset = (uint64_t)(base + offset); *result = (int64_t)descriptor->offset; return FACET_OK;
    }
    if (descriptor->kind != DESCRIPTOR_FILE) { *error = ESPIPE; return FACET_OK; }
    IFile *file = proxy_from_borrowed(&IFile_MetaData,
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
    IPageAllocator *allocator = proxy_from_borrowed(
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
    IPageAllocator *allocator = proxy_from_borrowed(
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
    IProcessLifecycle *lifecycle = proxy_from_borrowed(
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
    view->descriptors[0] = (PosixDescriptor){.kind=DESCRIPTOR_INPUT,.handle=stdin_handle};
    view->descriptors[1] = (PosixDescriptor){.kind=DESCRIPTOR_OUTPUT,.handle=stdout_handle};
    view->descriptors[2] = (PosixDescriptor){.kind=DESCRIPTOR_OUTPUT,.handle=stdout_handle};
    view->interface = (IPOSIXView){
        .self = view, .priv = view, .getInterface = get_interface,
        .write_fd = posix_write, .read_fd = posix_read,
        .open_fd = posix_open, .close_fd = posix_close,
        .seek_fd = posix_seek, .allocate_pages = posix_allocate_pages,
        .free_pages = posix_free_pages, .exit_process = posix_exit,
        .get_domain_id = posix_get_domain_id, .get_pid = posix_get_pid,
        .get_cwd = posix_get_cwd, .change_directory = posix_change_directory,
        .list_directory = posix_list_directory,
        .authenticate = posix_authenticate, .spawn_process = posix_spawn,
        .spawn_inherited = posix_spawn_inherited,
        .wait_process = posix_wait,
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

int dominit0_posix_view_bind_process_control(Dominit0PosixView *view,
    void *context, uint64_t domain_id, int32_t pid, FacetHandle default_session, Dominit0PosixSpawn spawn,
    Dominit0PosixWait wait)
{
    if (view == NULL || spawn == NULL || wait == NULL) return -1;
    view->process_context = context;
    view->domain_id = domain_id;
    view->pid = pid;
    if (default_session.platform != NULL &&
        libfacet_handle_clone(default_session, &view->default_session) != FACET_OK)
        return -1;
    view->spawn = spawn;
    view->wait = wait;
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
    if (view->default_session.platform != NULL)
        (void)libfacet_handle_release(view->default_session);
    free(view);
}
