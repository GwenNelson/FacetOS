#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/ISession.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/allocator.h"

static void *resolve(IProcessEnvironment *environment, const char *name,
                     const FacetInterfaceMeta *metadata)
{
    FacetString key = {.data = name, .length = strlen(name)};
    FacetHandle handle = {0};
    if (environment->resolve(environment->self, &key, &handle) != FACET_OK)
        return NULL;
    return libfacet_proxy_from_handle(metadata, handle);
}

static bool write_text(IByteWriter *output, const char *text)
{
    FacetArray_u8 bytes = {
        .data = (uint8_t *)(uintptr_t)text, .count = strlen(text),
    };
    uint32_t written = 0;
    return output->write_bytes(output->self, &bytes, &written) == FACET_OK &&
           written == bytes.count;
}

static bool can_read(IDirectory *cwd, const char *path)
{
    FacetString name = {.data = path, .length = strlen(path)};
    FacetHandle handle = {0};
    if (cwd->open_file(cwd->self, &name, &handle) != FACET_OK) return false;
    IFile *file = libfacet_proxy_from_handle(&IFile_MetaData, handle);
    FacetArray_u8 data = {0};
    FacetResult result = file == NULL ? FACET_INVALID_HANDLE :
        file->read_at(file->self, 0, 64, &data);
    free(data.data);
    libfacet_free_proxy_client(file);
    return result == FACET_OK;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDirectory_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IFile_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISession_MetaData) != FACET_OK)
        return 1;
    IGenericObject *root = NULL;
    if (platform_init(&argc, &argv, &root) != FACET_OK) return 1;
    IProcessEnvironment *environment = libfacet_proxy_client_get_interface(
        root, IID_IProcessEnvironment);
    IPageAllocator *allocator = resolve(environment, "memory.pages",
                                        &IPageAllocator_MetaData);
    IByteWriter *output = resolve(environment, "stdout", &IByteWriter_MetaData);
    ISession *session = resolve(environment, "session", &ISession_MetaData);
    FacetHandle cwd_handle = {0};
    if (environment == NULL || allocator == NULL || output == NULL ||
        session == NULL || facet_app_allocator_use_pages(allocator) != 0 ||
        environment->get_cwd(environment->self, &cwd_handle) != FACET_OK)
        return 1;
    IDirectory *cwd = libfacet_proxy_from_handle(&IDirectory_MetaData,
                                                  cwd_handle);
    uint32_t uid = UINT32_MAX, gid = UINT32_MAX;
    bool admin = false;
    bool identity = session->get_credentials(session->self, &uid, &gid,
                                              &admin) == FACET_OK;
    bool public_ok = can_read(cwd, "/Data/TestData/public.txt");
    bool user_ok = can_read(cwd, "/Data/TestData/user-only.txt");
    bool root_ok = can_read(cwd, "/Data/TestData/root-only.txt");
    bool hidden_ok = can_read(cwd, "/Data/TestData/root-private/inside.txt");
    bool expected = identity && public_ok &&
        (uid == 0 ? user_ok && root_ok && hidden_ok :
         uid == 1000 && user_ok && !root_ok && !hidden_ok);
    (void)write_text(output, expected ?
        "native TestPerms: PASS\r\n" : "native TestPerms: FAIL\r\n");
    libfacet_free_proxy_client(cwd);
    libfacet_free_proxy_client(session);
    libfacet_free_proxy_client(output);
    libfacet_free_proxy_client(allocator);
    libfacet_free_proxy_client(environment);
    return expected ? 0 : 1;
}
