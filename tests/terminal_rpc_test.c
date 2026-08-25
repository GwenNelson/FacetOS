#include <facetos/config.h>
#include <facetos/dominit0/config.h>
#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/logging.h>
#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/terminal.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/libfacet/platform.h>

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
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
    *handle = (FakeHandle){.context = context, .dispatch = dispatch,
                           .references = 1};
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
    FacetRpcMessage generated = {.protocol_version = FACET_RPC_PROTOCOL_VERSION};
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
    (void)value;
    (void)out;
    return FACET_NOT_SUPPORTED;
}

FacetResult libfacet_platform_method_handle(FacetHandle object,
                                            uint32_t method_id,
                                            FacetHandle *out)
{
    (void)object;
    (void)method_id;
    (void)out;
    return FACET_NOT_SUPPORTED;
}

void klog(enum log_level level, const char *format, ...)
{
    (void)level;
    (void)format;
}

FacetResult dominit0_logging_emit(ILoggingConfig *config, uint64_t domain_id,
                                  int32_t level, FacetString component,
                                  FacetString message)
{
    (void)config;
    (void)domain_id;
    (void)level;
    (void)component;
    (void)message;
    return FACET_OK;
}

static bool serial_ready;
static uint8_t serial_byte;
static uint8_t serial_written[32];
static size_t serial_written_count;
static PlatformConsoleKey local_key;
static bool local_key_ready;
static uint16_t presented[80 * 25];

int platform_serial_initialize(void) { return 0; }

int platform_serial_read_byte(uint8_t *byte)
{
    if (!serial_ready) return 1;
    *byte = serial_byte;
    serial_ready = false;
    return 0;
}

int platform_serial_write(const uint8_t *data, size_t size)
{
    assert(size <= sizeof(serial_written) - serial_written_count);
    memcpy(serial_written + serial_written_count, data, size);
    serial_written_count += size;
    return 0;
}

int platform_local_console_initialize(void) { return 0; }

int platform_local_console_present(const uint16_t *cells, size_t count)
{
    assert(count == 80 * 25);
    memcpy(presented, cells, count * sizeof(*cells));
    return 0;
}

int platform_local_console_poll_key(PlatformConsoleKey *key)
{
    if (!local_key_ready) return 1;
    *key = local_key;
    local_key_ready = false;
    return 0;
}

static IProcessEnvironment *make_environment(Dominit0SystemConfig *system,
                                             size_t domain, size_t assignment,
                                             Dominit0ProcessEnvironment **server_out)
{
    Dominit0ProcessEnvironment *server = dominit0_process_environment_create(
        system->current_domains[domain]->environment, (FacetHandle){0});
    assert(server != NULL);
    assert(dominit0_terminal_bind_process_environment(
               system->current_domains[domain], assignment, server) == 0);
    FacetHandle root = dominit0_process_environment_root_handle(server);
    FacetHandle copy = {0};
    assert(libfacet_handle_clone(root, &copy) == FACET_OK);
    IProcessEnvironment *client = libfacet_new_proxy_client(
        &IProcessEnvironment_MetaData, copy);
    assert(client != NULL);
    *server_out = server;
    return client;
}

static void *resolve(IProcessEnvironment *environment, const char *name,
                     const FacetInterfaceMeta *metadata)
{
    FacetString key = {.data = name, .length = strlen(name)};
    FacetHandle handle = {0};
    assert(environment->resolve(environment->self, &key, &handle) == FACET_OK);
    void *object = libfacet_new_proxy_client(metadata, handle);
    assert(object != NULL);
    return object;
}

static void destroy_environment(IProcessEnvironment *client,
                                Dominit0ProcessEnvironment *server)
{
    libfacet_free_proxy_client(client);
    dominit0_process_environment_destroy(server);
}

int main(void)
{
    FacetSystemConfig parsed;
    FacetConfigDiagnostic diagnostic;
    Dominit0SystemConfig system;
    assert(facet_config_make_fallback(&parsed, &diagnostic) == 0);
    assert(dominit0_config_objects_init(&system, &parsed, &diagnostic) == 0);
    assert(dominit0_environment_initialize(&system) == 0);
    assert(dominit0_terminal_initialize(&system) == 0);

    Dominit0ProcessEnvironment *serial_server = NULL;
    IProcessEnvironment *serial_environment = make_environment(
        &system, 0, 0, &serial_server);
    IByteReader *serial_input = resolve(serial_environment, "stdin",
                                        &IByteReader_MetaData);
    IByteWriter *serial_output = resolve(serial_environment, "stdout",
                                         &IByteWriter_MetaData);
    serial_byte = 'x';
    serial_ready = true;
    FacetArray_u8 bytes = {0};
    assert(serial_input->read_bytes(serial_input->self, 1, &bytes) == FACET_OK);
    assert(bytes.count == 1 && bytes.data[0] == 'x');
    free(bytes.data);
    FacetArray_u8 output = {.data = (uint8_t *)"ok", .count = 2};
    uint32_t written = 0;
    assert(serial_output->write_bytes(serial_output->self, &output, &written) ==
           FACET_OK);
    assert(written == 2 && serial_written_count == 2);
    assert(memcmp(serial_written, "ok", 2) == 0);
    libfacet_free_proxy_client(serial_output);
    libfacet_free_proxy_client(serial_input);
    destroy_environment(serial_environment, serial_server);

    Dominit0ProcessEnvironment *tty1_server = NULL;
    IProcessEnvironment *tty1_environment = make_environment(
        &system, 0, 1, &tty1_server);
    IByteReader *tty1_input = resolve(tty1_environment, "stdin",
                                      &IByteReader_MetaData);
    IByteWriter *tty1_output = resolve(tty1_environment, "stdout",
                                       &IByteWriter_MetaData);
    output = (FacetArray_u8){.data = (uint8_t *)"A", .count = 1};
    assert(tty1_output->write_bytes(tty1_output->self, &output, &written) ==
           FACET_OK);
    assert((presented[0] & 0xffu) == 'A');
    local_key = (PlatformConsoleKey){.kind = PLATFORM_CONSOLE_KEY_BYTE,
                                     .byte = 'z'};
    local_key_ready = true;
    bytes = (FacetArray_u8){0};
    assert(tty1_input->read_bytes(tty1_input->self, 1, &bytes) == FACET_OK);
    assert(bytes.count == 1 && bytes.data[0] == 'z');
    free(bytes.data);

    Dominit0ProcessEnvironment *tty2_server = NULL;
    IProcessEnvironment *tty2_environment = make_environment(
        &system, 1, 0, &tty2_server);
    IByteReader *tty2_input = resolve(tty2_environment, "stdin",
                                      &IByteReader_MetaData);
    IByteWriter *tty2_output = resolve(tty2_environment, "stdout",
                                       &IByteWriter_MetaData);
    output = (FacetArray_u8){.data = (uint8_t *)"B", .count = 1};
    assert(tty2_output->write_bytes(tty2_output->self, &output, &written) ==
           FACET_OK);
    assert((presented[0] & 0xffu) == 'A');
    local_key = (PlatformConsoleKey){
        .kind = PLATFORM_CONSOLE_KEY_SWITCH_TERMINAL, .terminal_index = 1};
    local_key_ready = true;
    bytes = (FacetArray_u8){0};
    assert(tty2_input->read_bytes(tty2_input->self, 1, &bytes) == FACET_OK);
    assert(bytes.count == 0);
    assert((presented[0] & 0xffu) == 'B');

    libfacet_free_proxy_client(tty2_output);
    libfacet_free_proxy_client(tty2_input);
    destroy_environment(tty2_environment, tty2_server);
    libfacet_free_proxy_client(tty1_output);
    libfacet_free_proxy_client(tty1_input);
    destroy_environment(tty1_environment, tty1_server);
    dominit0_terminal_destroy();
    dominit0_environment_destroy(&system);
    dominit0_config_objects_destroy(&system);
    return 0;
}
