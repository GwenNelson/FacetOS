#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/IProcessLifecycle.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/allocator.h"
#include "filesystem_error.h"

static void *resolve(IProcessEnvironment *environment, const char *name,
                     const FacetInterfaceMeta *metadata)
{
    FacetString key = {.data = name, .length = strlen(name)};
    FacetHandle handle = {0};
    if (environment->resolve(environment->self, &key, &handle) != FACET_OK)
        return NULL;
    return libfacet_proxy_from_handle(metadata, handle);
}

static int write_data(IByteWriter *output, const void *data, size_t size)
{
    FacetArray_u8 bytes = {.data = (uint8_t *)(uintptr_t)data, .count = size};
    uint32_t written = 0;
    return output->write_bytes(output->self, &bytes, &written) == FACET_OK &&
           written == size ? 0 : -1;
}

static int cat_stream(IByteReader *input, IByteWriter *output, bool numbered,
                      uint64_t *line_number)
{
    bool line_start = true;
    for (;;) {
        FacetArray_u8 bytes = {0};
        if (input->read_bytes(input->self, 256, &bytes) != FACET_OK) return 1;
        if (bytes.count == 0) { free(bytes.data); (void)platform_yield(); continue; }
        for (size_t i = 0; i < bytes.count; i++) {
            if (numbered && line_start) {
                char number[32];
                int length = snprintf(number, sizeof(number), "%6llu\t",
                                      (unsigned long long)(*line_number)++);
                if (length > 0) (void)write_data(output, number, (size_t)length);
                line_start = false;
            }
            (void)write_data(output, &bytes.data[i], 1);
            if (bytes.data[i] == '\n') line_start = true;
        }
        free(bytes.data);
    }
}

static FacetResult cat_file(IDirectory *cwd, IByteWriter *output,
                            const char *path, bool numbered,
                            uint64_t *line_number)
{
    FacetString name = {.data = path, .length = strlen(path)};
    FacetHandle handle = {0};
    FacetResult result = cwd->open_file(cwd->self, &name, &handle);
    if (result != FACET_OK) return result;
    IFile *file = libfacet_proxy_from_handle(&IFile_MetaData, handle);
    uint64_t size = 0;
    result = file == NULL ? FACET_INVALID_HANDLE :
        file->get_size(file->self, &size);
    if (result != FACET_OK) {
        libfacet_free_proxy_client(file);
        return result;
    }
    uint64_t offset = 0;
    bool line_start = true;
    while (offset < size) {
        FacetArray_u8 bytes = {0};
        result = file->read_at(file->self, offset, 256, &bytes);
        if (result != FACET_OK || bytes.count == 0) {
            free(bytes.data);
            libfacet_free_proxy_client(file);
            return result == FACET_OK ? FACET_ERROR : result;
        }
        for (size_t i = 0; i < bytes.count; i++) {
            if (numbered && line_start) {
                char number[32];
                int length = snprintf(number, sizeof(number), "%6llu\t",
                                      (unsigned long long)(*line_number)++);
                if (length > 0) (void)write_data(output, number, (size_t)length);
                line_start = false;
            }
            (void)write_data(output, &bytes.data[i], 1);
            if (bytes.data[i] == '\n') line_start = true;
        }
        offset += bytes.count;
        free(bytes.data);
    }
    libfacet_free_proxy_client(file);
    return FACET_OK;
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteReader_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDirectory_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IFile_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessLifecycle_MetaData) != FACET_OK)
        return 1;
    IGenericObject *root = NULL;
    if (platform_init(&argc, &argv, &root) != FACET_OK) return 1;
    IProcessEnvironment *environment = libfacet_proxy_client_get_interface(
        root, IID_IProcessEnvironment);
    IPageAllocator *allocator = resolve(environment, "memory.pages",
                                        &IPageAllocator_MetaData);
    IByteReader *input = resolve(environment, "stdin", &IByteReader_MetaData);
    IByteWriter *output = resolve(environment, "stdout", &IByteWriter_MetaData);
    IProcessLifecycle *lifecycle = resolve(environment, "process.lifecycle",
                                            &IProcessLifecycle_MetaData);
    FacetHandle cwd_handle = {0};
    if (environment == NULL || allocator == NULL || input == NULL ||
        output == NULL || lifecycle == NULL ||
        environment->get_cwd(environment->self, &cwd_handle) != FACET_OK ||
        facet_app_allocator_use_pages(allocator) != 0)
        return 1;
    IDirectory *cwd = libfacet_proxy_from_handle(&IDirectory_MetaData,
                                                  cwd_handle);
    bool numbered = false, options = true;
    int first = argc;
    for (int i = 1; i < argc; i++) {
        if (options && strcmp(argv[i], "--") == 0) { options = false; continue; }
        if (options && strcmp(argv[i], "-n") == 0) { numbered = true; continue; }
        first = i; break;
    }
    int status = 0;
    uint64_t line_number = 1;
    if (first == argc)
        status = cat_stream(input, output, numbered, &line_number);
    else for (int i = first; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0)
            status |= cat_stream(input, output, numbered, &line_number);
        else {
            FacetResult result = cat_file(cwd, output, argv[i], numbered,
                                          &line_number);
            if (result == FACET_OK) continue;
            (void)write_data(output, "cat: ", 5);
            (void)write_data(output, argv[i], strlen(argv[i]));
            (void)write_data(output, ": ", 2);
            const char *message = facet_filesystem_error(result);
            (void)write_data(output, message, strlen(message));
            (void)write_data(output, "\r\n", 2);
            status = 1;
        }
    }
    libfacet_free_proxy_client(cwd);
    (void)lifecycle->notify_exit(lifecycle->self, status);
    return status;
}
