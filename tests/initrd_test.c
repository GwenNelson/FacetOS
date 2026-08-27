#include <facetos/initrd.h>
#include <facetos/dominit0/file_view.h>
#include <facetos/dominit0/posix.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/libfacet/platform.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FakeHandle {
    void *context;
    FacetPlatformDispatch dispatch;
    size_t references;
} FakeHandle;

typedef struct TestStreams {
    IByteReader reader;
    IByteWriter writer;
    FacetHandle reader_handle;
    FacetHandle writer_handle;
    uint8_t output[64];
    size_t output_count;
} TestStreams;

static FacetResult stream_interface(void *self, uuid_t iid, FacetHandle *out)
{
    TestStreams *streams = self;
    FacetHandle handle = {0};
    if (memcmp(iid.bytes, IID_IByteReader.bytes, sizeof(iid.bytes)) == 0)
        handle = streams->reader_handle;
    else if (memcmp(iid.bytes, IID_IByteWriter.bytes, sizeof(iid.bytes)) == 0)
        handle = streams->writer_handle;
    if (out == NULL || handle.platform == NULL) return FACET_INVALID_ARGUMENT;
    *out = handle;
    return FACET_OK;
}

static FacetResult stream_read(void *self, uint32_t maximum,
                               FacetArray_u8 *out)
{
    (void)self;
    (void)maximum;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetArray_u8){0};
    return FACET_OK;
}

static FacetResult stream_write(void *self, const FacetArray_u8 *data,
                                uint32_t *written)
{
    TestStreams *streams = self;
    if (data == NULL || written == NULL ||
        data->count > sizeof(streams->output) - streams->output_count)
        return FACET_INVALID_ARGUMENT;
    memcpy(streams->output + streams->output_count, data->data, data->count);
    streams->output_count += data->count;
    *written = (uint32_t)data->count;
    return FACET_OK;
}

static FacetResult unused_spawn(void *context, const FacetString *path,
                                const FacetArray_string *argv,
                                FacetHandle session, int32_t *pid,
                                int32_t *error)
{
    (void)context; (void)path; (void)argv; (void)session;
    if (pid != NULL) *pid = -1;
    if (error != NULL) *error = ENOSYS;
    return FACET_OK;
}

static FacetResult unused_wait(void *context, int32_t pid, int32_t *status,
                               int32_t *error)
{
    (void)context; (void)pid;
    if (status != NULL) *status = -1;
    if (error != NULL) *error = ECHILD;
    return FACET_OK;
}

FacetResult libfacet_platform_export(void *context, FacetPlatformDispatch dispatch,
                                     FacetHandle *out)
{
    FakeHandle *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) return FACET_OUT_OF_MEMORY;
    handle->context = context;
    handle->dispatch = dispatch;
    handle->references = 1;
    out->platform = handle;
    return FACET_OK;
}

FacetResult libfacet_platform_unexport(FacetHandle handle)
{
    return libfacet_platform_handle_release(handle);
}

FacetResult libfacet_platform_handle_clone(FacetHandle source,
                                           FacetHandle *destination)
{
    if (source.platform == NULL || destination == NULL)
        return FACET_INVALID_HANDLE;
    FakeHandle *handle = source.platform;
    handle->references++;
    destination->platform = handle;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_release(FacetHandle source)
{
    FakeHandle *handle = source.platform;
    if (handle == NULL || handle->references == 0) return FACET_INVALID_HANDLE;
    if (--handle->references == 0) free(handle);
    return FACET_OK;
}

FacetResult libfacet_platform_call(FacetHandle target,
                                   const FacetRpcMessage *request,
                                   FacetRpcMessage *reply)
{
    FakeHandle *handle = target.platform;
    if (handle == NULL || request == NULL || reply == NULL)
        return FACET_INVALID_ARGUMENT;
    FacetRpcMessage generated = {0};
    generated.protocol_version = FACET_RPC_PROTOCOL_VERSION;
    FacetResult result = handle->dispatch(handle->context, request, &generated);
    if (generated.word_count == 0)
        generated.words[generated.word_count++] = (uint64_t)(int64_t)result;
    for (size_t i = 0; i < generated.attachment_count; i++) {
        if (generated.attachments[i].handle.platform == NULL) continue;
        FacetHandle clone = {0};
        if (libfacet_platform_handle_clone(generated.attachments[i].handle,
                                           &clone) != FACET_OK)
            return FACET_OUT_OF_MEMORY;
        generated.attachments[i].handle = clone;
    }
    *reply = generated;
    return FACET_OK;
}

FacetResult libfacet_platform_handle_from(uint64_t value, FacetHandle *out)
{
    (void)value; (void)out;
    return FACET_NOT_SUPPORTED;
}

FacetResult libfacet_platform_method_handle(FacetHandle object,
                                            uint32_t method_id,
                                            FacetHandle *out)
{
    (void)object; (void)method_id; (void)out;
    return FACET_NOT_SUPPORTED;
}

typedef struct Archive {
    uint8_t bytes[4096];
    size_t size;
} Archive;

static void append_owned_entry(Archive *archive, const char *name,
                               unsigned mode, unsigned uid, unsigned gid,
                               const void *data, size_t size)
{
    char header[111];
    int length = snprintf(header, sizeof(header),
        "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
        1, mode, uid, gid, 1, 0, (unsigned)size, 0, 0, 0, 0,
        (unsigned)(strlen(name) + 1), 0);
    assert(length == 110);
    memcpy(archive->bytes + archive->size, header, 110);
    archive->size += 110;
    memcpy(archive->bytes + archive->size, name, strlen(name) + 1);
    archive->size += strlen(name) + 1;
    while ((archive->size & 3u) != 0) archive->bytes[archive->size++] = 0;
    if (size != 0) memcpy(archive->bytes + archive->size, data, size);
    archive->size += size;
    while ((archive->size & 3u) != 0) archive->bytes[archive->size++] = 0;
}

static void append_entry(Archive *archive, const char *name, unsigned mode,
                         const void *data, size_t size)
{
    append_owned_entry(archive, name, mode, 0, 0, data, size);
}

static Archive valid_archive(void)
{
    Archive archive = {0};
    static const char contents[] = "hello from initrd\n";
    append_entry(&archive, ".", 0040755, NULL, 0);
    append_entry(&archive, "README", 0100644, contents, sizeof(contents) - 1);
    append_entry(&archive, "dir", 0040755, NULL, 0);
    append_entry(&archive, "dir/file", 0100644, "x", 1);
    append_owned_entry(&archive, "owner-tool", 0100750, 1000, 1000, "x", 1);
    append_entry(&archive, "not-executable", 0100644, "x", 1);
    append_owned_entry(&archive, "private", 0040700, 1000, 1000, NULL, 0);
    append_owned_entry(&archive, "private/tool", 0100755, 1000, 1000,
                       "x", 1);
    append_owned_entry(&archive, "private/secret", 0100600, 1000, 1000,
                       "secret", 6);
    append_entry(&archive, "TRAILER!!!", 0, NULL, 0);
    return archive;
}

static FacetString facet_string(const char *value)
{
    return (FacetString){.data = value, .length = strlen(value)};
}

static IDirectory *store_directory(IFileStore *store, const char *path)
{
    FacetString name = facet_string(path);
    FacetHandle handle = {0};
    assert(store->open_directory(store->self, &name, &handle) == FACET_OK);
    IDirectory *directory = libfacet_new_proxy_client(&IDirectory_MetaData,
                                                       handle);
    assert(directory != NULL);
    return directory;
}

static IDirectory *child_directory(IDirectory *base, const char *path)
{
    FacetString name = facet_string(path);
    FacetHandle handle = {0};
    assert(base->open_directory(base->self, &name, &handle) == FACET_OK);
    IDirectory *directory = libfacet_new_proxy_client(&IDirectory_MetaData,
                                                       handle);
    assert(directory != NULL);
    return directory;
}

static IFile *child_file(IDirectory *base, const char *path)
{
    FacetString name = facet_string(path);
    FacetHandle handle = {0};
    assert(base->open_file(base->self, &name, &handle) == FACET_OK);
    IFile *file = libfacet_new_proxy_client(&IFile_MetaData, handle);
    assert(file != NULL);
    return file;
}

static void assert_directory_path(IDirectory *directory, const char *expected)
{
    FacetString path = {0};
    assert(directory->getpath(directory->self, &path) == FACET_OK);
    assert(path.length == strlen(expected));
    assert(memcmp(path.data, expected, path.length) == 0);
    free((void *)(uintptr_t)path.data);
}

static void test_path_resolution_rpc(IFileStore *store)
{
    FacetHandle handle = {0};
    FacetString name = facet_string("README");
    assert(store->open_file(store->self, &name, &handle) == FACET_OK);
    IFile *root_readme = libfacet_new_proxy_client(&IFile_MetaData, handle);
    assert(root_readme != NULL);
    libfacet_free_proxy_client(root_readme);

    IDirectory *root_relative_dir = store_directory(store, "dir");
    assert_directory_path(root_relative_dir, "/dir");
    libfacet_free_proxy_client(root_relative_dir);

    name = facet_string("./README");
    handle = (FacetHandle){0};
    assert(store->open_file(store->self, &name, &handle) == FACET_OK);
    root_readme = libfacet_new_proxy_client(&IFile_MetaData, handle);
    assert(root_readme != NULL);
    libfacet_free_proxy_client(root_readme);
    name = facet_string("README/");
    handle = (FacetHandle){0};
    assert(store->open_file(store->self, &name, &handle) == FACET_NOT_FOUND);

    const char *root_aliases[] = {".", "..", "///", "/dir/.."};
    for (size_t i = 0; i < sizeof(root_aliases) / sizeof(root_aliases[0]); i++) {
        IDirectory *root = store_directory(store, root_aliases[i]);
        assert_directory_path(root, "/");
        libfacet_free_proxy_client(root);
    }

    IDirectory *root = store_directory(store, "/");
    IDirectory *dir = child_directory(root, "dir/");
    assert_directory_path(dir, "/dir");

    const char *file_aliases[] = {
        "file", "./file", "../README", "/README", "//dir//file",
        "../../README"
    };
    for (size_t i = 0; i < sizeof(file_aliases) / sizeof(file_aliases[0]); i++) {
        IFile *file = child_file(dir, file_aliases[i]);
        libfacet_free_proxy_client(file);
    }

    IDirectory *same = child_directory(dir, ".");
    assert_directory_path(same, "/dir");
    libfacet_free_proxy_client(same);
    IDirectory *parent = child_directory(dir, "..");
    assert_directory_path(parent, "/");
    libfacet_free_proxy_client(parent);
    IDirectory *absolute = child_directory(dir, "/dir//");
    assert_directory_path(absolute, "/dir");
    libfacet_free_proxy_client(absolute);

    const char *directory_required[] = {"file/", "file/.", "file/.."};
    for (size_t i = 0;
         i < sizeof(directory_required) / sizeof(directory_required[0]); i++) {
        name = facet_string(directory_required[i]);
        handle = (FacetHandle){0};
        assert(dir->open_file(dir->self, &name, &handle) == FACET_NOT_FOUND);
        assert(handle.platform == NULL);
    }

    name = facet_string("README");
    assert(root->open_directory(root->self, &name, &handle) == FACET_NOT_FOUND);
    name = facet_string("dir");
    assert(root->open_file(root->self, &name, &handle) == FACET_NOT_FOUND);

    FacetString empty = {.data = "", .length = 0};
    assert(root->open_directory(root->self, &empty, &handle) ==
           FACET_INVALID_ARGUMENT);
    const char embedded_data[] = {'d', 'i', 'r', '\0', 'x'};
    FacetString embedded = {.data = embedded_data,
                            .length = sizeof(embedded_data)};
    assert(root->open_directory(root->self, &embedded, &handle) ==
           FACET_INVALID_ARGUMENT);

    char *overlong = malloc(4097);
    assert(overlong != NULL);
    memset(overlong, 'a', 4097);
    FacetString too_long = {.data = overlong, .length = 4097};
    assert(root->open_directory(root->self, &too_long, &handle) ==
           FACET_INVALID_ARGUMENT);
    free(overlong);

    char *overflowing = malloc(4095);
    assert(overflowing != NULL);
    memset(overflowing, 'a', 4094);
    overflowing[4094] = '\0';
    FacetString overflow_path = {.data = overflowing, .length = 4094};
    assert(dir->open_directory(dir->self, &overflow_path, &handle) ==
           FACET_INVALID_ARGUMENT);
    free(overflowing);

    libfacet_free_proxy_client(dir);
    libfacet_free_proxy_client(root);
}

static void test_lookup_and_read_only_rpc(void)
{
    Archive archive = valid_archive();
    FacetInitrd *initrd = facet_initrd_create(archive.bytes, archive.size);
    assert(initrd != NULL);
    const uint8_t *data = NULL;
    size_t size = 0;
    assert(facet_initrd_find_file(initrd, "/README", &data, &size) == FACET_OK);
    assert(size == strlen("hello from initrd\n"));
    assert(memcmp(data, "hello from initrd\n", size) == 0);
    assert(facet_initrd_find_file(initrd, "README", &data, &size) == FACET_NOT_FOUND);
    assert(facet_initrd_find_file(initrd, "/missing", &data, &size) == FACET_NOT_FOUND);
    assert(facet_initrd_check_execute(initrd, "/owner-tool", 1000, 1000,
                                      false) == FACET_OK);
    assert(facet_initrd_check_execute(initrd, "/owner-tool", 2000, 2000,
                                      false) == FACET_ACCESS_DENIED);
    assert(facet_initrd_check_execute(initrd, "/owner-tool", 42, 42,
                                      true) == FACET_OK);
    assert(facet_initrd_check_execute(initrd, "/not-executable", 0, 0,
                                      true) == FACET_ACCESS_DENIED);
    assert(facet_initrd_check_execute(initrd, "/private/tool", 1000, 1000,
                                      false) == FACET_OK);
    assert(facet_initrd_check_execute(initrd, "/private/tool", 2000, 2000,
                                      false) == FACET_ACCESS_DENIED);

    FacetHandle store_handle = {0};
    assert(facet_initrd_export(initrd, &store_handle) == FACET_OK);
    FacetHandle store_client_handle = {0};
    assert(libfacet_handle_clone(store_handle, &store_client_handle) == FACET_OK);
    IFileStore *store = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                  store_client_handle);

    Dominit0CredentialFileStore *owner_view =
        dominit0_credential_file_store_create(initrd, 1000, 1000, false);
    Dominit0CredentialFileStore *other_view =
        dominit0_credential_file_store_create(initrd, 2000, 2000, false);
    assert(owner_view != NULL && other_view != NULL);
    FacetHandle view_handle = {0};
    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(owner_view),
               &view_handle) == FACET_OK);
    IFileStore *owner_store = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                        view_handle);
    FacetString secret_path = facet_string("/private/secret");
    FacetHandle secret_handle = {0};
    assert(owner_store->open_file(owner_store->self, &secret_path,
                                  &secret_handle) == FACET_OK);
    IFile *secret = libfacet_new_proxy_client(&IFile_MetaData, secret_handle);
    FacetArray_u8 secret_data = {0};
    assert(secret->read_at(secret->self, 0, 16, &secret_data) == FACET_OK);
    assert(secret_data.count == 6 &&
           memcmp(secret_data.data, "secret", 6) == 0);
    free(secret_data.data);
    libfacet_free_proxy_client(secret);
    libfacet_free_proxy_client(owner_store);

    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(other_view),
               &view_handle) == FACET_OK);
    IFileStore *other_store = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                        view_handle);
    secret_handle = (FacetHandle){0};
    assert(other_store->open_file(other_store->self, &secret_path,
                                  &secret_handle) == FACET_ACCESS_DENIED);
    assert(secret_handle.platform == NULL);
    libfacet_free_proxy_client(other_store);
    FacetString path = {.data = "/README", .length = 7};
    FacetHandle file_handle = {0};
    assert(store->open_file(store->self, &path, &file_handle) == FACET_OK);
    IFile *file = libfacet_new_proxy_client(&IFile_MetaData, file_handle);
    uint64_t file_size = 0;
    assert(file->get_size(file->self, &file_size) == FACET_OK);
    assert(file_size == strlen("hello from initrd\n"));
    FacetArray_u8 read = {0};
    assert(file->read_at(file->self, 6, 4, &read) == FACET_OK);
    assert(read.count == 4);
    assert(memcmp(read.data, "from", 4) == 0);
    free(read.data);
    FacetArray_u8 payload = {.data = (uint8_t *)"no", .count = 2};
    uint32_t written = 99;
    assert(file->write_at(file->self, 0, &payload, &written) == FACET_NOT_SUPPORTED);
    libfacet_free_proxy_client(file);

    FacetString root_path = {.data = "/", .length = 1};
    FacetHandle directory_handle = {0};
    assert(store->open_directory(store->self, &root_path,
                                 &directory_handle) == FACET_OK);
    IDirectory *directory = libfacet_new_proxy_client(&IDirectory_MetaData,
                                                       directory_handle);
    FacetArray_Entry entries = {0};
    uint64_t next = 0;
    bool end = false;
    assert(directory->list(directory->self, 0, 16, &entries, &next, &end) ==
           FACET_OK);
    assert(end);
    assert(entries.count == 5);
    assert(entries.data[0].name.length == strlen("README"));
    assert(memcmp(entries.data[0].name.data, "README",
                  entries.data[0].name.length) == 0);
    assert(entries.data[0].type == EntryType_File);
    assert(entries.data[1].name.length == strlen("dir"));
    assert(memcmp(entries.data[1].name.data, "dir",
                  entries.data[1].name.length) == 0);
    assert(entries.data[1].type == EntryType_Directory);
    facet_rpc_release_value(FACET_TYPE_ARRAY,
                            &FacetArray_Entry_TypeMeta, &entries);

    IDirectory *nested = store_directory(store, "/dir");
    entries = (FacetArray_Entry){0};
    next = 0;
    end = false;
    assert(nested->list(nested->self, 0, 16, &entries, &next, &end) ==
           FACET_OK);
    assert(end);
    assert(entries.count == 1);
    assert(entries.data[0].name.length == strlen("file"));
    assert(memcmp(entries.data[0].name.data, "file",
                  entries.data[0].name.length) == 0);
    assert(entries.data[0].type == EntryType_File);
    facet_rpc_release_value(FACET_TYPE_ARRAY,
                            &FacetArray_Entry_TypeMeta, &entries);
    libfacet_free_proxy_client(nested);
    libfacet_free_proxy_client(directory);
    test_path_resolution_rpc(store);
    libfacet_free_proxy_client(store);
    dominit0_credential_file_store_destroy(other_view);
    dominit0_credential_file_store_destroy(owner_view);
    facet_initrd_destroy(initrd);
}

static void test_malformed_archives(void)
{
    Archive bad_magic = valid_archive();
    bad_magic.bytes[0] = 'x';
    assert(facet_initrd_create(bad_magic.bytes, bad_magic.size) == NULL);

    Archive traversal = {0};
    append_entry(&traversal, "../escape", 0100644, "x", 1);
    append_entry(&traversal, "TRAILER!!!", 0, NULL, 0);
    assert(facet_initrd_create(traversal.bytes, traversal.size) == NULL);

    Archive unsupported = {0};
    append_entry(&unsupported, "link", 0120777, "target", 6);
    append_entry(&unsupported, "TRAILER!!!", 0, NULL, 0);
    assert(facet_initrd_create(unsupported.bytes, unsupported.size) == NULL);
}

static void test_posix_descriptor_lifetimes(void)
{
    Archive archive = valid_archive();
    FacetInitrd *initrd = facet_initrd_create(archive.bytes, archive.size);
    assert(initrd != NULL);
    Dominit0CredentialFileStore *view_store =
        dominit0_credential_file_store_create(initrd, 1000, 1000, false);
    assert(view_store != NULL);
    FacetHandle store_copy = {0};
    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(view_store),
               &store_copy) == FACET_OK);
    IFileStore *store = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                  store_copy);
    assert(store != NULL);
    FacetString root_path = facet_string("/");
    FacetHandle cwd = {0};
    assert(store->open_directory(store->self, &root_path, &cwd) == FACET_OK);
    libfacet_free_proxy_client(store);

    TestStreams streams = {0};
    streams.reader = (IByteReader){
        .self = &streams, .priv = &streams,
        .getInterface = stream_interface, .read_bytes = stream_read,
    };
    streams.writer = (IByteWriter){
        .self = &streams, .priv = &streams,
        .getInterface = stream_interface, .write_bytes = stream_write,
    };
    assert(libfacet_export_interface(&streams.reader, &IByteReader_MetaData,
                                     &streams.reader_handle) == FACET_OK);
    assert(libfacet_export_interface(&streams.writer, &IByteWriter_MetaData,
                                     &streams.writer_handle) == FACET_OK);

    /* An absolute path must use the namespace root's credential view, not an
     * inherited CWD capability.  This models login handing a formerly
     * privileged CWD to an unprivileged child. */
    Dominit0CredentialFileStore *restricted_store =
        dominit0_credential_file_store_create(initrd, 2000, 2000, false);
    assert(restricted_store != NULL);
    FacetHandle owner_store_copy = {0};
    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(view_store),
               &owner_store_copy) == FACET_OK);
    IFileStore *owner_files = libfacet_new_proxy_client(
        &IFileStore_MetaData, owner_store_copy);
    FacetString private_path = facet_string("/private");
    FacetHandle privileged_cwd = {0};
    assert(owner_files != NULL &&
           owner_files->open_directory(owner_files->self, &private_path,
                                       &privileged_cwd) == FACET_OK);
    libfacet_free_proxy_client(owner_files);
    FacetHandle restricted_store_copy = {0};
    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(restricted_store),
               &restricted_store_copy) == FACET_OK);
    IFileStore *restricted_files = libfacet_new_proxy_client(
        &IFileStore_MetaData, restricted_store_copy);
    FacetHandle restricted_root = {0};
    FacetHandle denied_private = {0};
    assert(restricted_files != NULL &&
           restricted_files->open_directory(restricted_files->self,
                                            &private_path,
                                            &denied_private) ==
               FACET_ACCESS_DENIED);
    assert(restricted_files != NULL &&
           restricted_files->open_directory(restricted_files->self,
                                            &root_path,
                                            &restricted_root) == FACET_OK);
    libfacet_free_proxy_client(restricted_files);
    Dominit0PosixView *restricted_view = dominit0_posix_view_create(
        streams.reader_handle, streams.writer_handle,
        dominit0_credential_file_store_handle(restricted_store),
        privileged_cwd);
    assert(restricted_view != NULL);
    assert(dominit0_posix_view_set_root(restricted_view, restricted_root) == 0);
    (void)libfacet_handle_release(privileged_cwd);
    (void)libfacet_handle_release(restricted_root);
    FacetHandle restricted_posix_copy = {0};
    assert(libfacet_handle_clone(dominit0_posix_view_handle(restricted_view),
                                 &restricted_posix_copy) == FACET_OK);
    IPOSIXView *restricted_posix = libfacet_new_proxy_client(
        &IPOSIXView_MetaData, restricted_posix_copy);
    FacetString private_secret = facet_string("/private/secret");
    int32_t restricted_fd = -1, restricted_error = 0;
    uint32_t restricted_mode = 0, restricted_uid = 0, restricted_gid = 0;
    assert(restricted_posix != NULL &&
           restricted_posix->stat_path(
               restricted_posix->self, &private_secret, &restricted_mode,
               &restricted_uid, &restricted_gid, &restricted_error) ==
               FACET_OK);
    assert(restricted_error == EACCES);
    assert(restricted_posix != NULL &&
           restricted_posix->open_fd(restricted_posix->self, &private_secret,
                                     O_RDONLY, 0, &restricted_fd,
                                     &restricted_error) == FACET_OK);
    assert(restricted_fd == -1 && restricted_error == EACCES);
    libfacet_free_proxy_client(restricted_posix);
    dominit0_posix_view_destroy(restricted_view);
    dominit0_credential_file_store_destroy(restricted_store);

    Dominit0PosixView *view = dominit0_posix_view_create(
        streams.reader_handle, streams.writer_handle,
        dominit0_credential_file_store_handle(view_store), cwd);
    assert(view != NULL);
    (void)libfacet_handle_release(cwd);
    FacetHandle posix_copy = {0};
    assert(libfacet_handle_clone(dominit0_posix_view_handle(view),
                                 &posix_copy) == FACET_OK);
    IPOSIXView *posix = libfacet_new_proxy_client(&IPOSIXView_MetaData,
                                                  posix_copy);
    assert(posix != NULL);

    uint32_t stat_mode = 0, stat_uid = UINT32_MAX, stat_gid = UINT32_MAX;
    int32_t stat_error = -1;
    FacetString stat_readme = facet_string("/README");
    assert(posix->stat_path(posix->self, &stat_readme, &stat_mode, &stat_uid,
                            &stat_gid, &stat_error) == FACET_OK);
    assert(stat_error == 0 && stat_mode == 0100644 && stat_uid == 0 &&
           stat_gid == 0);

    FacetArray_u8 message = {.data = (uint8_t *)"ok", .count = 2};
    int64_t io_result = -1;
    int32_t io_error = -1;
    assert(posix->write_fd(posix->self, 1, &message, &io_result,
                           &io_error) == FACET_OK);
    assert(io_result == 2 && io_error == 0);
    assert(posix->write_fd(posix->self, 1, &message, &io_result,
                           &io_error) == FACET_OK);
    assert(io_result == 2 && streams.output_count == 4);

    FacetString readme = facet_string("/README");
    int32_t fd = -1;
    assert(posix->open_fd(posix->self, &readme, O_RDONLY, 0,
                          &fd, &io_error) == FACET_OK);
    assert(fd >= 3 && io_error == 0);
    FacetArray_u8 bytes = {0};
    assert(posix->read_fd(posix->self, fd, 5, &bytes, &io_result,
                          &io_error) == FACET_OK);
    assert(io_result == 5 && bytes.count == 5 &&
           memcmp(bytes.data, "hello", 5) == 0);
    free(bytes.data);
    assert(posix->seek_fd(posix->self, fd, 0, SEEK_SET, &io_result,
                          &io_error) == FACET_OK);
    assert(io_result == 0 && io_error == 0);
    bytes = (FacetArray_u8){0};
    assert(posix->read_fd(posix->self, fd, 5, &bytes, &io_result,
                          &io_error) == FACET_OK);
    assert(io_result == 5 && memcmp(bytes.data, "hello", 5) == 0);
    free(bytes.data);
    int32_t close_result = -1;
    assert(posix->close_fd(posix->self, fd, &close_result,
                           &io_error) == FACET_OK);
    assert(close_result == 0 && io_error == 0);
    assert(posix->close_fd(posix->self, fd, &close_result,
                           &io_error) == FACET_OK);
    assert(close_result == -1 && io_error == EBADF);

    libfacet_free_proxy_client(posix);
    dominit0_posix_view_destroy(view);
    assert(libfacet_unexport_interface(streams.writer_handle) == FACET_OK);
    assert(libfacet_unexport_interface(streams.reader_handle) == FACET_OK);
    dominit0_credential_file_store_destroy(view_store);
    facet_initrd_destroy(initrd);
}

static void test_chrooted_posix_synthetic_etc(void)
{
    Archive archive = {0};
    append_entry(&archive, ".", 0040755, NULL, 0);
    append_entry(&archive, "posix", 0040755, NULL, 0);
    append_entry(&archive, "posix/bin", 0040755, NULL, 0);
    append_entry(&archive, "posix/bin/sh", 0100755, "elf", 3);
    append_entry(&archive, "posix/home", 0040755, NULL, 0);
    append_entry(&archive, "posix/home/root", 0040755, NULL, 0);
    append_entry(&archive, "TRAILER!!!", 0, NULL, 0);
    FacetInitrd *initrd = facet_initrd_create(archive.bytes, archive.size);
    assert(initrd != NULL);
    Dominit0CredentialFileStore *view_store =
        dominit0_credential_file_store_create(initrd, 1000, 1000, false);
    assert(view_store != NULL);
    /* Native clients use the same domain-local mount as IPOSIXView.  This
     * deliberately exercises IFileStore directly: /posix/etc is not an
     * initrd entry and must nevertheless be discoverable and readable. */
    FacetHandle native_store_handle = {0};
    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(view_store),
               &native_store_handle) == FACET_OK);
    IFileStore *native_store = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                         native_store_handle);
    assert(native_store != NULL);
    FacetString native_posix = facet_string("/posix");
    FacetHandle native_posix_handle = {0};
    assert(native_store->open_directory(native_store->self, &native_posix,
                                        &native_posix_handle) == FACET_OK);
    IDirectory *native_posix_dir = libfacet_new_proxy_client(&IDirectory_MetaData,
                                                              native_posix_handle);
    assert(native_posix_dir != NULL);
    FacetArray_Entry posix_entries = {0};
    uint64_t posix_next = 0;
    bool posix_end = false;
    assert(native_posix_dir->list(native_posix_dir->self, 0, 8, &posix_entries,
                                  &posix_next, &posix_end) == FACET_OK);
    bool saw_native_etc = false;
    for (size_t i = 0; i < posix_entries.count; i++)
        saw_native_etc |= posix_entries.data[i].name.length == 3 &&
            memcmp(posix_entries.data[i].name.data, "etc", 3) == 0;
    assert(saw_native_etc);
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_Entry_TypeMeta,
                            &posix_entries);
    libfacet_free_proxy_client(native_posix_dir);
    FacetString native_etc = facet_string("/posix/etc");
    FacetHandle native_etc_handle = {0};
    assert(native_store->open_directory(native_store->self, &native_etc,
                                        &native_etc_handle) == FACET_OK);
    IDirectory *native_etc_dir = libfacet_new_proxy_client(&IDirectory_MetaData,
                                                            native_etc_handle);
    assert(native_etc_dir != NULL);
    FacetArray_Entry native_entries = {0};
    uint64_t native_next = 0;
    bool native_end = false;
    assert(native_etc_dir->list(native_etc_dir->self, 0, 8, &native_entries,
                                &native_next, &native_end) == FACET_OK);
    assert(native_entries.count == 3 && native_end);
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_Entry_TypeMeta,
                            &native_entries);
    FacetString native_passwd = facet_string("passwd");
    FacetHandle native_passwd_handle = {0};
    assert(native_etc_dir->open_file(native_etc_dir->self, &native_passwd,
                                     &native_passwd_handle) == FACET_OK);
    IFile *native_passwd_file = libfacet_new_proxy_client(&IFile_MetaData,
                                                           native_passwd_handle);
    assert(native_passwd_file != NULL);
    FacetArray_u8 native_bytes = {0};
    assert(native_passwd_file->read_at(native_passwd_file->self, 0, 128,
                                       &native_bytes) == FACET_OK);
    assert(native_bytes.count >= 10 &&
           memcmp(native_bytes.data, "root:x:0:0", 10) == 0);
    free(native_bytes.data);
    libfacet_free_proxy_client(native_passwd_file);
    FacetString native_shadow = facet_string("shadow");
    FacetHandle native_shadow_handle = {0};
    assert(native_etc_dir->open_file(native_etc_dir->self, &native_shadow,
                                     &native_shadow_handle) == FACET_OK);
    IFile *native_shadow_file = libfacet_new_proxy_client(&IFile_MetaData,
                                                           native_shadow_handle);
    assert(native_shadow_file != NULL);
    native_bytes = (FacetArray_u8){0};
    assert(native_shadow_file->read_at(native_shadow_file->self, 0, 128,
                                       &native_bytes) == FACET_ACCESS_DENIED);
    libfacet_free_proxy_client(native_shadow_file);
    FacetString native_parent = facet_string("..");
    FacetHandle native_parent_handle = {0};
    assert(native_etc_dir->open_directory(native_etc_dir->self, &native_parent,
                                          &native_parent_handle) == FACET_OK);
    IDirectory *native_parent_dir = libfacet_new_proxy_client(
        &IDirectory_MetaData, native_parent_handle);
    FacetString native_parent_path = {0};
    assert(native_parent_dir != NULL &&
           native_parent_dir->getpath(native_parent_dir->self,
                                      &native_parent_path) == FACET_OK);
    assert(native_parent_path.length == 6 &&
           memcmp(native_parent_path.data, "/posix", 6) == 0);
    free((void *)(uintptr_t)native_parent_path.data);
    libfacet_free_proxy_client(native_parent_dir);
    libfacet_free_proxy_client(native_etc_dir);
    libfacet_free_proxy_client(native_store);
    FacetHandle store_copy = {0};
    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(view_store),
               &store_copy) == FACET_OK);
    IFileStore *store = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                  store_copy);
    assert(store != NULL);
    FacetString backing = facet_string("/posix");
    FacetHandle cwd = {0}, inherited_etc = {0};
    assert(store->open_directory(store->self, &backing, &cwd) == FACET_OK);
    assert(store->open_directory(store->self, &native_etc,
                                 &inherited_etc) == FACET_OK);
    libfacet_free_proxy_client(store);

    TestStreams streams = {0};
    streams.reader = (IByteReader){
        .self = &streams, .priv = &streams,
        .getInterface = stream_interface, .read_bytes = stream_read,
    };
    streams.writer = (IByteWriter){
        .self = &streams, .priv = &streams,
        .getInterface = stream_interface, .write_bytes = stream_write,
    };
    assert(libfacet_export_interface(&streams.reader, &IByteReader_MetaData,
                                     &streams.reader_handle) == FACET_OK);
    assert(libfacet_export_interface(&streams.writer, &IByteWriter_MetaData,
                                     &streams.writer_handle) == FACET_OK);
    Dominit0PosixView *view = dominit0_posix_view_create(
        streams.reader_handle, streams.writer_handle,
        dominit0_credential_file_store_handle(view_store), cwd);
    assert(view != NULL);
    FacetHandle posix_copy = {0};
    assert(libfacet_handle_clone(dominit0_posix_view_handle(view),
                                 &posix_copy) == FACET_OK);
    IPOSIXView *posix = libfacet_new_proxy_client(&IPOSIXView_MetaData,
                                                  posix_copy);
    assert(posix != NULL);

    int32_t error = -1;
    FacetArray_string entries = {0};
    FacetString root = facet_string("/");
    assert(posix->list_directory(posix->self, &root, &entries, &error) == FACET_OK);
    assert(error == 0 && entries.count == 3);
    bool saw_bin = false, saw_etc = false, saw_home = false;
    for (size_t i = 0; i < entries.count; i++) {
        saw_bin |= entries.data[i].length == 3 &&
            memcmp(entries.data[i].data, "bin", 3) == 0;
        saw_etc |= entries.data[i].length == 3 &&
            memcmp(entries.data[i].data, "etc", 3) == 0;
        saw_home |= entries.data[i].length == 4 &&
            memcmp(entries.data[i].data, "home", 4) == 0;
    }
    assert(saw_bin && saw_etc && saw_home);
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_string_TypeMeta,
                            &entries);

    FacetString passwd = facet_string("/etc/passwd");
    int32_t fd = -1;
    assert(posix->open_fd(posix->self, &passwd, O_RDONLY, 0, &fd, &error) ==
           FACET_OK);
    assert(fd >= 3 && error == 0);
    FacetArray_u8 bytes = {0};
    int64_t result = -1;
    assert(posix->read_fd(posix->self, fd, 128, &bytes, &result, &error) ==
           FACET_OK);
    assert(error == 0 && result >= 10 && bytes.count >= 10 &&
           memcmp(bytes.data, "root:x:0:0", 10) == 0);
    free(bytes.data);
    int32_t close_result = -1;
    assert(posix->close_fd(posix->self, fd, &close_result, &error) == FACET_OK);
    FacetString shadow = facet_string("/etc/shadow");
    uint32_t shadow_mode = 0, shadow_uid = UINT32_MAX,
             shadow_gid = UINT32_MAX;
    assert(posix->stat_path(posix->self, &shadow, &shadow_mode, &shadow_uid,
                            &shadow_gid, &error) == FACET_OK);
    assert(error == 0 && shadow_mode == 0100600 && shadow_uid == 0 &&
           shadow_gid == 0);
    assert(posix->open_fd(posix->self, &shadow, O_RDONLY, 0, &fd, &error) ==
           FACET_OK);
    assert(fd == -1 && error == EACCES);

    /* The virtual credentials use the same modular crypt format as the
     * configured authentication source; only the privileged view may read
     * this file, so the unprivileged open above remains the access check. */

    FacetString etc = facet_string("/etc");
    assert(posix->change_directory(posix->self, &etc, &error) == FACET_OK &&
           error == 0);
    FacetString cwd_path = {0};
    assert(posix->get_cwd(posix->self, &cwd_path, &error) == FACET_OK &&
           error == 0 && cwd_path.length == 4 &&
           memcmp(cwd_path.data, "/etc", 4) == 0);
    free((void *)(uintptr_t)cwd_path.data);
    FacetString parent = facet_string("..");
    entries = (FacetArray_string){0};
    assert(posix->list_directory(posix->self, &parent, &entries, &error) ==
               FACET_OK && error == 0);
    bool parent_has_bin = false, parent_has_etc = false;
    for (size_t i = 0; i < entries.count; i++) {
        parent_has_bin |= entries.data[i].length == 3 &&
            memcmp(entries.data[i].data, "bin", 3) == 0;
        parent_has_etc |= entries.data[i].length == 3 &&
            memcmp(entries.data[i].data, "etc", 3) == 0;
    }
    assert(parent_has_bin && parent_has_etc);
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_string_TypeMeta,
                            &entries);
    assert(posix->change_directory(posix->self, &parent, &error) == FACET_OK &&
           error == 0);
    assert(posix->get_cwd(posix->self, &cwd_path, &error) == FACET_OK &&
           error == 0 && cwd_path.length == 1 && cwd_path.data[0] == '/');
    free((void *)(uintptr_t)cwd_path.data);

    libfacet_free_proxy_client(posix);
    dominit0_posix_view_destroy(view);

    /* An external POSIX command launched by a native shell can inherit the
     * mounted /posix/etc directory without the shell's synthetic-CWD flag.
     * Listing "." there must not mistake the inherited CWD for namespace /
     * and append a second, nested etc entry. */
    view = dominit0_posix_view_create(
        streams.reader_handle, streams.writer_handle,
        dominit0_credential_file_store_handle(view_store), inherited_etc);
    assert(view != NULL);
    (void)libfacet_handle_release(inherited_etc);
    assert(dominit0_posix_view_set_root(view, cwd) == 0);
    (void)libfacet_handle_release(cwd);
    assert(libfacet_handle_clone(dominit0_posix_view_handle(view),
                                 &posix_copy) == FACET_OK);
    posix = libfacet_new_proxy_client(&IPOSIXView_MetaData, posix_copy);
    assert(posix != NULL);
    FacetString dot = facet_string(".");
    entries = (FacetArray_string){0};
    error = -1;
    assert(posix->list_directory(posix->self, &dot, &entries, &error) ==
               FACET_OK && error == 0 && entries.count == 3);
    bool inherited_has_fstab = false, inherited_has_passwd = false,
         inherited_has_shadow = false, inherited_has_etc = false;
    for (size_t i = 0; i < entries.count; i++) {
        inherited_has_fstab |= entries.data[i].length == 5 &&
            memcmp(entries.data[i].data, "fstab", 5) == 0;
        inherited_has_passwd |= entries.data[i].length == 6 &&
            memcmp(entries.data[i].data, "passwd", 6) == 0;
        inherited_has_shadow |= entries.data[i].length == 6 &&
            memcmp(entries.data[i].data, "shadow", 6) == 0;
        inherited_has_etc |= entries.data[i].length == 3 &&
            memcmp(entries.data[i].data, "etc", 3) == 0;
    }
    assert(inherited_has_fstab && inherited_has_passwd &&
           inherited_has_shadow && !inherited_has_etc);
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_string_TypeMeta,
                            &entries);
    libfacet_free_proxy_client(posix);
    dominit0_posix_view_destroy(view);

    /* Root sees the same mounted object through both native and POSIX
     * namespaces.  This is the complement of the unprivileged denials above. */
    Dominit0CredentialFileStore *root_store =
        dominit0_credential_file_store_create(initrd, 0, 0, true);
    assert(root_store != NULL);
    FacetHandle root_store_handle = {0};
    assert(libfacet_handle_clone(
               dominit0_credential_file_store_handle(root_store),
               &root_store_handle) == FACET_OK);
    IFileStore *root_files = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                       root_store_handle);
    assert(root_files != NULL);
    FacetString native_root_shadow = facet_string("/posix/etc/shadow");
    FacetHandle root_shadow_handle = {0};
    assert(root_files->open_file(root_files->self, &native_root_shadow,
                                 &root_shadow_handle) == FACET_OK);
    IFile *root_shadow = libfacet_new_proxy_client(&IFile_MetaData,
                                                   root_shadow_handle);
    assert(root_shadow != NULL);
    bytes = (FacetArray_u8){0};
    assert(root_shadow->read_at(root_shadow->self, 0, 128, &bytes) == FACET_OK);
    assert(bytes.count >= 10 && memcmp(bytes.data, "root:$5$", 8) == 0);
    free(bytes.data);
    libfacet_free_proxy_client(root_shadow);

    FacetString root_backing = facet_string("/posix");
    FacetHandle root_cwd = {0};
    assert(root_files->open_directory(root_files->self, &root_backing,
                                      &root_cwd) == FACET_OK);
    libfacet_free_proxy_client(root_files);
    Dominit0PosixView *root_view = dominit0_posix_view_create(
        streams.reader_handle, streams.writer_handle,
        dominit0_credential_file_store_handle(root_store), root_cwd);
    assert(root_view != NULL);
    assert(dominit0_posix_view_bind_process_control(
               root_view, NULL, 0, 1, true, unused_spawn, unused_wait,
               NULL) == 0);
    FacetHandle root_posix_handle = {0};
    assert(libfacet_handle_clone(dominit0_posix_view_handle(root_view),
                                 &root_posix_handle) == FACET_OK);
    IPOSIXView *root_posix = libfacet_new_proxy_client(&IPOSIXView_MetaData,
                                                       root_posix_handle);
    assert(root_posix != NULL);
    assert(root_posix->open_fd(root_posix->self, &shadow, O_RDONLY, 0, &fd,
                               &error) == FACET_OK);
    assert(fd >= 3 && error == 0);
    bytes = (FacetArray_u8){0};
    assert(root_posix->read_fd(root_posix->self, fd, 128, &bytes, &result,
                               &error) == FACET_OK);
    assert(result >= 10 && bytes.count >= 10 &&
           memcmp(bytes.data, "root:$5$", 8) == 0);
    free(bytes.data);
    assert(root_posix->close_fd(root_posix->self, fd, &close_result,
                                 &error) == FACET_OK);
    FacetString root_home = facet_string("/home/root");
    assert(root_posix->change_directory(root_posix->self, &root_home, &error) ==
               FACET_OK &&
           error == 0);
    assert(root_posix->get_cwd(root_posix->self, &cwd_path, &error) == FACET_OK &&
           error == 0 && cwd_path.length == strlen("/home/root") &&
           memcmp(cwd_path.data, "/home/root", cwd_path.length) == 0);
    free((void *)(uintptr_t)cwd_path.data);
    assert(dominit0_posix_view_set_root(root_view, root_cwd) == 0);
    assert(root_posix->get_cwd(root_posix->self, &cwd_path, &error) == FACET_OK &&
           error == 0 && cwd_path.length == strlen("/home/root") &&
           memcmp(cwd_path.data, "/home/root", cwd_path.length) == 0);
    free((void *)(uintptr_t)cwd_path.data);
    (void)libfacet_handle_release(root_cwd);
    libfacet_free_proxy_client(root_posix);
    dominit0_posix_view_destroy(root_view);
    dominit0_credential_file_store_destroy(root_store);
    assert(libfacet_unexport_interface(streams.writer_handle) == FACET_OK);
    assert(libfacet_unexport_interface(streams.reader_handle) == FACET_OK);
    dominit0_credential_file_store_destroy(view_store);
    facet_initrd_destroy(initrd);
}

int main(void)
{
    test_lookup_and_read_only_rpc();
    test_posix_descriptor_lifetimes();
    test_chrooted_posix_synthetic_etc();
    test_malformed_archives();
    return 0;
}
