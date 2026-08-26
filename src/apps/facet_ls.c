#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IProcessLifecycle.h>
#include <facetos/interfaces/IUnixMetadata.h>

#include <stdint.h>
#include <stdio.h>
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

static int write_bytes(IByteWriter *output, const char *text, size_t length)
{
    FacetArray_u8 bytes = {.data = (uint8_t *)(uintptr_t)text,
                           .count = length};
    uint32_t written = 0;
    return output->write_bytes(output->self, &bytes, &written) == FACET_OK &&
           written == length ? 0 : -1;
}

static int write_text(IByteWriter *output, const char *text)
{
    return write_bytes(output, text, strlen(text));
}

static IDirectory *open_directory(IDirectory *cwd, const char *path)
{
    if (path == NULL) return cwd;
    FacetString value = {.data = path, .length = strlen(path)};
    FacetHandle handle = {0};
    if (cwd->open_directory(cwd->self, &value, &handle) != FACET_OK)
        return NULL;
    return libfacet_proxy_from_handle(&IDirectory_MetaData, handle);
}

static void write_metadata(IDirectory *directory, const Entry *entry,
                           IByteWriter *output)
{
    FacetHandle object = {0};
    FacetResult result;
    if (entry->type == EntryType_Directory)
        result = directory->open_directory(directory->self, &entry->name,
                                           &object);
    else
        result = directory->open_file(directory->self, &entry->name, &object);
    IGenericObject *generic = result == FACET_OK ?
        libfacet_proxy_from_handle(&IGenericObject_MetaData, object) : NULL;
    IUnixMetadata *metadata = generic == NULL ? NULL :
        libfacet_proxy_client_get_interface(generic, IID_IUnixMetadata);
    uint32_t mode = entry->type == EntryType_Directory ? 0040755 : 0100644;
    uint32_t uid = 0, gid = 0;
    if (metadata != NULL) {
        (void)metadata->getmode(metadata->self, &mode);
        (void)metadata->getuid(metadata->self, &uid);
        (void)metadata->getgid(metadata->self, &gid);
    }
    char details[48];
    int length = snprintf(details, sizeof(details), "%06o %u %u ", mode,
                          uid, gid);
    if (length > 0) (void)write_bytes(output, details, (size_t)length);
    libfacet_free_proxy_client(metadata);
    libfacet_free_proxy_client(generic);
}

static int list_one(IDirectory *cwd, IByteWriter *output, const char *path,
                    bool long_format, bool show_all, bool heading)
{
    IDirectory *directory = open_directory(cwd, path);
    if (directory == NULL) {
        (void)write_text(output, "ls: cannot open ");
        (void)write_text(output, path == NULL ? "." : path);
        (void)write_text(output, "\r\n");
        return 1;
    }
    if (heading) {
        (void)write_text(output, path == NULL ? "." : path);
        (void)write_text(output, ":\r\n");
    }
    uint64_t cursor = 0;
    bool end = false;
    while (!end) {
        FacetArray_Entry entries = {0};
        uint64_t next = cursor;
        if (directory->list(directory->self, cursor, 32, &entries,
                            &next, &end) != FACET_OK)
            break;
        for (size_t i = 0; i < entries.count; i++) {
            if (!show_all && entries.data[i].name.length != 0 &&
                entries.data[i].name.data[0] == '.')
                continue;
            if (long_format) write_metadata(directory, &entries.data[i], output);
            (void)write_bytes(output, entries.data[i].name.data,
                              entries.data[i].name.length);
            if (entries.data[i].type == EntryType_Directory)
                (void)write_text(output, "/");
            (void)write_text(output, "\r\n");
        }
        facet_rpc_release_value(FACET_TYPE_ARRAY,
                                &FacetArray_Entry_TypeMeta, &entries);
        if (next == cursor && !end) break;
        cursor = next;
    }
    if (path != NULL) libfacet_free_proxy_client(directory);
    return 0;
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDirectory_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IFile_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IUnixMetadata_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessLifecycle_MetaData) != FACET_OK)
        return 1;
    IGenericObject *root = NULL;
    if (platform_init(&argc, &argv, &root) != FACET_OK) return 1;
    IProcessEnvironment *environment = libfacet_proxy_client_get_interface(
        root, IID_IProcessEnvironment);
    IPageAllocator *allocator = resolve(environment, "memory.pages",
                                        &IPageAllocator_MetaData);
    IByteWriter *output = resolve(environment, "stdout", &IByteWriter_MetaData);
    IProcessLifecycle *lifecycle = resolve(environment, "process.lifecycle",
                                            &IProcessLifecycle_MetaData);
    FacetHandle cwd_handle = {0};
    if (environment == NULL || allocator == NULL || output == NULL ||
        lifecycle == NULL ||
        environment->get_cwd(environment->self, &cwd_handle) != FACET_OK ||
        facet_app_allocator_use_pages(allocator) != 0)
        return 1;
    IDirectory *cwd = libfacet_proxy_from_handle(&IDirectory_MetaData,
                                                  cwd_handle);
    bool long_format = false, show_all = false, options = true;
    int status = 0;
    int first_path = argc;
    for (int i = 1; i < argc; i++) {
        if (options && strcmp(argv[i], "--") == 0) { options = false; continue; }
        if (options && argv[i][0] == '-' && argv[i][1] != '\0') {
            for (const char *flag = argv[i] + 1; *flag != '\0'; flag++) {
                if (*flag == 'l') long_format = true;
                else if (*flag == 'a') show_all = true;
                else { (void)write_text(output, "ls: invalid option\r\n"); status = 1; goto done; }
            }
            continue;
        }
        first_path = i;
        break;
    }
    if (first_path == argc) status = list_one(cwd, output, NULL, long_format,
                                              show_all, false);
    else for (int i = first_path; i < argc; i++) {
        if (i != first_path) (void)write_text(output, "\r\n");
        status |= list_one(cwd, output, argv[i], long_format, show_all,
                           argc - first_path > 1);
    }
done:
    libfacet_free_proxy_client(cwd);
    (void)lifecycle->notify_exit(lifecycle->self, status);
    return status;
}
