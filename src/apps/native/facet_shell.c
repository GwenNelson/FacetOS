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
#include <facetos/interfaces/IProcessLifecycle.h>
#include <facetos/interfaces/IProcess.h>
#include <facetos/interfaces/IProcessManager.h>
#include <facetos/interfaces/ISession.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/allocator.h"
#include "shell_parser.h"

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

static FacetResult directory_path(IDirectory *directory, FacetString *path)
{
    *path = (FacetString){0};
    return directory == NULL ? FACET_INVALID_HANDLE :
        directory->getpath(directory->self, path);
}

static IDirectory *open_directory(IDirectory *cwd, const char *path)
{
    FacetString name = {.data = path, .length = strlen(path)};
    FacetHandle handle = {0};
    if (cwd == NULL ||
        cwd->open_directory(cwd->self, &name, &handle) != FACET_OK)
        return NULL;
    return libfacet_proxy_from_handle(&IDirectory_MetaData, handle);
}

static void command_pwd(IDirectory *cwd, IByteWriter *output)
{
    FacetString path = {0};
    if (directory_path(cwd, &path) != FACET_OK) return;
    (void)write_string(output, path);
    (void)write_text(output, "\r\n");
    free((void *)(uintptr_t)path.data);
}

static void write_prompt(IDirectory *cwd, IByteWriter *output)
{
    FacetString path = {0};
    if (directory_path(cwd, &path) == FACET_OK) {
        (void)write_string(output, path);
        free((void *)(uintptr_t)path.data);
    } else {
        (void)write_text(output, "?");
    }
    (void)write_text(output, "> ");
}

static void command_cd(IDirectory **cwd, IByteWriter *output, const char *path)
{
    IDirectory *replacement = open_directory(*cwd, path);
    if (replacement == NULL) {
        (void)write_text(output, "cd: directory not found\r\n");
        return;
    }
    libfacet_free_proxy_client(*cwd);
    *cwd = replacement;
}

static bool file_exists(IDirectory *cwd, const char *path)
{
    FacetString name = {.data = path, .length = strlen(path)};
    FacetHandle handle = {0};
    if (cwd->open_file(cwd->self, &name, &handle) != FACET_OK)
        return false;
    (void)libfacet_handle_release(handle);
    return true;
}

static const char *environment_value(const FacetArray_string *environment,
                                     const char *name)
{
    size_t length = strlen(name);
    for (size_t i = 0; environment != NULL && i < environment->count; i++) {
        FacetString value = environment->data[i];
        if (value.length > length && value.data != NULL &&
            memcmp(value.data, name, length) == 0 && value.data[length] == '=')
            return value.data + length + 1;
    }
    return NULL;
}

static char *resolve_program(IDirectory *cwd, IDirectory *root,
                             const char *command,
                             const char *path)
{
    if (strchr(command, '/') != NULL) {
        IDirectory *base = command[0] == '/' ? root : cwd;
        return file_exists(base, command) ? strdup(command) : NULL;
    }
    if (path == NULL || *path == '\0') path = "/FacetOS";
    size_t offset = 0;
    while (offset != SIZE_MAX) {
        char *candidate = facet_shell_path_candidate(path, &offset, command);
        if (candidate == NULL) break;
        if (file_exists(root, candidate)) return candidate;
        free(candidate);
    }
    return NULL;
}

static void execute_program(IProcessManager *processes,
                            IProcessEnvironment *environment,
                            IDirectory *cwd, IDirectory *root,
                            IByteWriter *output,
                            char *arguments[], size_t count,
                            const char *path)
{
    char *program = resolve_program(cwd, root, arguments[0], path);
    if (program == NULL) {
        (void)write_text(output, "command not found\r\n");
        return;
    }
    FacetHandle environment_handle = {0};
    FacetHandle cwd_handle = {0};
    FacetHandle process_handle = {0};
    if (environment->getInterface(environment->self, IID_IProcessEnvironment,
                                  &environment_handle) != FACET_OK ||
        cwd->getInterface(cwd->self, IID_IDirectory, &cwd_handle) != FACET_OK) {
        free(program);
        (void)write_text(output, "unable to capture process context\r\n");
        return;
    }
    if (environment->set_cwd(environment->self, cwd_handle) != FACET_OK) {
        (void)libfacet_handle_release(environment_handle);
        (void)libfacet_handle_release(cwd_handle);
        free(program);
        (void)write_text(output, "unable to update current directory\r\n");
        return;
    }
    FacetString executable = {.data = program, .length = strlen(program)};
    FacetString values[32];
    for (size_t i = 0; i < count; i++)
        values[i] = (FacetString){.data = arguments[i],
                                  .length = strlen(arguments[i])};
    values[0] = executable;
    FacetArray_string argv = {.data = values, .count = count};
    FacetResult result = processes->launch(
        processes->self, &executable, &argv, environment_handle,
        &process_handle);
    (void)libfacet_handle_release(environment_handle);
    (void)libfacet_handle_release(cwd_handle);
    free(program);
    if (result != FACET_OK) {
        char code[] = "unable to start program (-00)\r\n";
        unsigned value = (unsigned)(result < 0 ? -result : result);
        code[26] = value >= 10 ? (char)('0' + value / 10) : ' ';
        code[27] = (char)('0' + value % 10);
        (void)write_text(output, code);
        return;
    }
    IProcess *process = libfacet_proxy_from_handle(&IProcess_MetaData,
                                                    process_handle);
    bool running = true;
    while (process != NULL && running) {
        if (process->getrunning(process->self, &running) != FACET_OK) break;
        if (running) (void)platform_yield();
    }
    libfacet_free_proxy_client(process);
}

static void shell_loop(IByteReader *input, IByteWriter *output,
                       IFileStore *files, ISession *session,
                       IProcessEnvironment *environment,
                       IProcessManager *processes)
{
    char line[512];
    FacetArray_string sysv_environment = {0};
    (void)environment->get_sysv_environment(environment->self,
                                            &sysv_environment);
    const char *path = environment_value(&sysv_environment, "PATH");
    FacetString root_name = {.data = "/", .length = 1};
    FacetHandle root_handle = {0};
    if (files->open_directory(files->self, &root_name, &root_handle) != FACET_OK) {
        (void)write_text(output, "FacetShell: unable to open root directory\r\n");
        return;
    }
    IDirectory *root = libfacet_proxy_from_handle(&IDirectory_MetaData,
                                                  root_handle);
    if (root == NULL) return;
    IDirectory *cwd = open_directory(root, ".");
    if (cwd == NULL) {
        libfacet_free_proxy_client(root);
        return;
    }
    (void)write_text(output, "FacetShell ready. Type help for commands.\r\n");
    for (;;) {
        write_prompt(cwd, output);
        if (read_line(input, output, line, sizeof(line)) != FACET_OK) break;
        char *arguments[32];
        size_t count = 0;
        if (facet_shell_parse_command(line, arguments, 32, &count) != 0) {
            (void)write_text(output, "syntax error: unmatched quote or escape\r\n");
            continue;
        }
        if (count == 0) continue;
        if (strcmp(arguments[0], "help") == 0)
            (void)write_text(output,
                "help whoami pwd cd [path] exit; other commands are external\r\n");
        else if (strcmp(arguments[0], "whoami") == 0)
            command_whoami(session, output);
        else if (strcmp(arguments[0], "pwd") == 0)
            command_pwd(cwd, output);
        else if (strcmp(arguments[0], "cd") == 0)
            command_cd(&cwd, output,
                       count > 1 ? arguments[1] : "/");
        else if (strcmp(arguments[0], "exit") == 0)
            break;
        else
            execute_program(processes, environment, cwd, root, output,
                            arguments, count, path);
    }
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_string_TypeMeta,
                            &sysv_environment);
    libfacet_free_proxy_client(cwd);
    libfacet_free_proxy_client(root);
}

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessLifecycle_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteReader_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IFileStore_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IFile_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDirectory_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISession_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPrincipal_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessManager_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcess_MetaData) != FACET_OK)
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
        {"process.lifecycle", &IProcessLifecycle_MetaData, NULL},
    };
    IPageAllocator *allocator = NULL;
    IByteReader *input = NULL;
    IByteWriter *output = NULL;
    IFileStore *files = NULL;
    ISession *session = NULL;
    IProcessLifecycle *lifecycle = NULL;
    IProcessManager *processes = NULL;
    void **objects[] = {(void **)&allocator, (void **)&input, (void **)&output,
                        (void **)&files, (void **)&session,
                        (void **)&lifecycle};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        FacetString name = {.data = required[i].name,
                            .length = strlen(required[i].name)};
        FacetHandle handle = {0};
        if (environment->resolve(environment->self, &name, &handle) != FACET_OK)
            return 1;
        *objects[i] = libfacet_proxy_from_handle(required[i].metadata, handle);
        if (*objects[i] == NULL) return 1;
    }
    FacetString process_name = {.data = "processes", .length = 9};
    FacetHandle process_manager_handle = {0};
    if (environment->resolve(environment->self, &process_name,
                             &process_manager_handle) != FACET_OK)
        return 1;
    processes = libfacet_proxy_from_handle(&IProcessManager_MetaData,
                                            process_manager_handle);
    if (processes == NULL) return 1;
    if (facet_app_allocator_use_pages(allocator) != 0) return 1;
    shell_loop(input, output, files, session, environment, processes);
    (void)lifecycle->notify_exit(lifecycle->self, 0);
    return 0;
}
