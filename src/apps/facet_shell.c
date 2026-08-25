#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IDirectory.h>
#include <facetos/interfaces/IFile.h>
#include <facetos/interfaces/IFileStore.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/interfaces/IPrincipal.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/interfaces/ISession.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../dominit/platform/allocator.h"

static FacetResult write_text(IByteWriter *output, const char *text)
{
    FacetArray_u8 bytes = {
        .data = (uint8_t *)(uintptr_t)text,
        .count = strlen(text),
    };
    uint32_t written = 0;
    FacetResult result = output->write_bytes(output->self, &bytes, &written);
    return result == FACET_OK && written == bytes.count ? FACET_OK : FACET_ERROR;
}

static FacetResult write_string(IByteWriter *output, FacetString text)
{
    FacetArray_u8 bytes = {
        .data = (uint8_t *)(uintptr_t)text.data,
        .count = text.length,
    };
    uint32_t written = 0;
    return output->write_bytes(output->self, &bytes, &written);
}

static FacetResult read_line(IByteReader *input, IByteWriter *output,
                             char *line, size_t capacity)
{
    size_t length = 0;
    for (;;) {
        FacetArray_u8 bytes = {0};
        FacetResult result = input->read_bytes(input->self, 1, &bytes);
        if (result != FACET_OK) return result;
        if (bytes.count == 0) {
            free(bytes.data);
            (void)platform_yield();
            continue;
        }
        uint8_t byte = bytes.data[0];
        free(bytes.data);
        if (byte == '\r' || byte == '\n') {
            line[length] = '\0';
            (void)write_text(output, "\r\n");
            return FACET_OK;
        }
        if (byte == 8 || byte == 127) {
            if (length != 0) {
                length--;
                (void)write_text(output, "\b \b");
            }
            continue;
        }
        if (byte < 32 || byte > 126 || length + 1 >= capacity) continue;
        line[length++] = (char)byte;
        char echo[2] = {(char)byte, '\0'};
        (void)write_text(output, echo);
    }
}

static void command_whoami(ISession *session, IByteWriter *output)
{
    FacetHandle principal_handle = {0};
    if (session->get_principal(session->self, &principal_handle) != FACET_OK) {
        (void)write_text(output, "unknown\r\n");
        return;
    }
    IPrincipal *principal = libfacet_proxy_from_handle(&IPrincipal_MetaData,
                                                       principal_handle);
    FacetString name = {0};
    if (principal == NULL ||
        principal->getname(principal->self, &name) != FACET_OK) {
        (void)write_text(output, "unknown\r\n");
    } else {
        (void)write_string(output, name);
        (void)write_text(output, "\r\n");
        free((void *)(uintptr_t)name.data);
    }
    libfacet_free_proxy_client(principal);
}

static void command_ls(IFileStore *files, IByteWriter *output, const char *path)
{
    FacetString directory_name = {.data = path, .length = strlen(path)};
    FacetHandle directory_handle = {0};
    if (files->open_directory(files->self, &directory_name,
                              &directory_handle) != FACET_OK) {
        (void)write_text(output, "ls: directory not found\r\n");
        return;
    }
    IDirectory *directory = libfacet_proxy_from_handle(&IDirectory_MetaData,
                                                       directory_handle);
    uint64_t cursor = 0;
    bool end = false;
    while (directory != NULL && !end) {
        FacetArray_Entry entries = {0};
        uint64_t next = cursor;
        if (directory->list(directory->self, cursor, 16, &entries,
                            &next, &end) != FACET_OK)
            break;
        for (size_t i = 0; i < entries.count; i++) {
            (void)write_string(output, entries.data[i].name);
            if (entries.data[i].type == EntryType_Directory)
                (void)write_text(output, "/");
            (void)write_text(output, "\r\n");
        }
        facet_rpc_release_value(FACET_TYPE_ARRAY,
                                &FacetArray_Entry_TypeMeta, &entries);
        if (next == cursor && !end) break;
        cursor = next;
    }
    libfacet_free_proxy_client(directory);
}

static void command_cat(IFileStore *files, IByteWriter *output, const char *path)
{
    FacetString file_name = {.data = path, .length = strlen(path)};
    FacetHandle file_handle = {0};
    if (files->open_file(files->self, &file_name, &file_handle) != FACET_OK) {
        (void)write_text(output, "cat: file not found\r\n");
        return;
    }
    IFile *file = libfacet_proxy_from_handle(&IFile_MetaData, file_handle);
    uint64_t size = 0;
    if (file == NULL || file->get_size(file->self, &size) != FACET_OK) {
        libfacet_free_proxy_client(file);
        return;
    }
    for (uint64_t offset = 0; offset < size;) {
        FacetArray_u8 bytes = {0};
        if (file->read_at(file->self, offset, 256, &bytes) != FACET_OK ||
            bytes.count == 0) {
            free(bytes.data);
            break;
        }
        uint32_t written = 0;
        (void)output->write_bytes(output->self, &bytes, &written);
        offset += bytes.count;
        free(bytes.data);
    }
    (void)write_text(output, "\r\n");
    libfacet_free_proxy_client(file);
}

static void shell_loop(IByteReader *input, IByteWriter *output,
                       IFileStore *files, ISession *session)
{
    char line[512];
    (void)write_text(output, "FacetShell ready. Type help for commands.\r\n");
    for (;;) {
        (void)write_text(output, "/> ");
        if (read_line(input, output, line, sizeof(line)) != FACET_OK) return;
        char *argument = strchr(line, ' ');
        if (argument != NULL) {
            *argument++ = '\0';
            while (*argument == ' ') argument++;
        }
        if (strcmp(line, "help") == 0)
            (void)write_text(output, "help whoami pwd ls [path] cat <path> exit\r\n");
        else if (strcmp(line, "whoami") == 0)
            command_whoami(session, output);
        else if (strcmp(line, "pwd") == 0)
            (void)write_text(output, "/\r\n");
        else if (strcmp(line, "ls") == 0)
            command_ls(files, output, argument != NULL && *argument ? argument : "/");
        else if (strcmp(line, "cat") == 0 && argument != NULL && *argument)
            command_cat(files, output, argument);
        else if (strcmp(line, "exit") == 0)
            return;
        else if (*line != '\0')
            (void)write_text(output, "Unknown command\r\n");
    }
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteReader_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IFileStore_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IFile_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDirectory_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISession_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPrincipal_MetaData) != FACET_OK)
        return 1;
    IGenericObject *root = NULL;
    if (platform_init(&argc, &argv, &root) != FACET_OK) return 1;
    IProcessEnvironment *environment =
        (IProcessEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IProcessEnvironment);
    if (environment == NULL) return 1;

    struct Required {
        const char *name;
        const FacetInterfaceMeta *metadata;
        void **object;
    } required[] = {
        {"memory.pages", &IPageAllocator_MetaData, NULL},
        {"stdin", &IByteReader_MetaData, NULL},
        {"stdout", &IByteWriter_MetaData, NULL},
        {"files", &IFileStore_MetaData, NULL},
        {"session", &ISession_MetaData, NULL},
    };
    IPageAllocator *allocator = NULL;
    IByteReader *input = NULL;
    IByteWriter *output = NULL;
    IFileStore *files = NULL;
    ISession *session = NULL;
    void **objects[] = {(void **)&allocator, (void **)&input, (void **)&output,
                        (void **)&files, (void **)&session};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        FacetString name = {.data = required[i].name,
                            .length = strlen(required[i].name)};
        FacetHandle handle = {0};
        if (environment->resolve(environment->self, &name, &handle) != FACET_OK)
            return 1;
        *objects[i] = libfacet_proxy_from_handle(required[i].metadata, handle);
        if (*objects[i] == NULL) return 1;
    }
    if (dominit_allocator_use_pages(allocator) != 0) return 1;
    shell_loop(input, output, files, session);
    return 0;
}
