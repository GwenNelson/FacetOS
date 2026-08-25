#include <facetos/initrd.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/libfacet/platform.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FakeHandle {
    void *context;
    FacetPlatformDispatch dispatch;
    size_t references;
} FakeHandle;

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

static void append_entry(Archive *archive, const char *name, unsigned mode,
                         const void *data, size_t size)
{
    char header[111];
    int length = snprintf(header, sizeof(header),
        "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
        1, mode, 0, 0, 1, 0, (unsigned)size, 0, 0, 0, 0,
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

static Archive valid_archive(void)
{
    Archive archive = {0};
    static const char contents[] = "hello from initrd\n";
    append_entry(&archive, ".", 0040755, NULL, 0);
    append_entry(&archive, "README", 0100644, contents, sizeof(contents) - 1);
    append_entry(&archive, "dir", 0040755, NULL, 0);
    append_entry(&archive, "dir/file", 0100644, "x", 1);
    append_entry(&archive, "TRAILER!!!", 0, NULL, 0);
    return archive;
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

    FacetHandle store_handle = {0};
    assert(facet_initrd_export(initrd, &store_handle) == FACET_OK);
    FacetHandle store_client_handle = {0};
    assert(libfacet_handle_clone(store_handle, &store_client_handle) == FACET_OK);
    IFileStore *store = libfacet_new_proxy_client(&IFileStore_MetaData,
                                                  store_client_handle);
    FacetString path = {.data = "/README", .length = 7};
    FacetHandle file_handle = {0};
    assert(store->open_file(store->self, &path, &file_handle) == FACET_OK);
    IFile *file = libfacet_new_proxy_client(&IFile_MetaData, file_handle);
    FacetArray_u8 payload = {.data = (uint8_t *)"no", .count = 2};
    uint32_t written = 99;
    assert(file->write_at(file->self, 0, &payload, &written) == FACET_NOT_SUPPORTED);
    libfacet_free_proxy_client(file);
    libfacet_free_proxy_client(store);
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

int main(void)
{
    test_lookup_and_read_only_rpc();
    test_malformed_archives();
    return 0;
}
