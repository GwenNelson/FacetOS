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
#include <facetos/interfaces/IProcessLifecycle.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/interfaces/ITerminal.h>
#include <facetos/interfaces/ITerminalControl.h>
#include <facetos/libfacet/platform.h>

#include <assert.h>
#include <stdbool.h>
#include <errno.h>
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

PlatformConfigSourceStatus platform_get_boot_module(
    const char *name, PlatformConfigSource *source)
{
    (void)name; (void)source;
    return PLATFORM_CONFIG_SOURCE_ABSENT;
}

void *platform_start_seat(CurrentSeat *seat, const void *elf_data,
                          size_t elf_size, FacetHandle *out_seat)
{
    (void)seat; (void)elf_data; (void)elf_size; (void)out_seat;
    return NULL;
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

typedef struct CredentialChange {
    uint32_t uid;
    uint32_t gid;
    bool set_uid;
    bool set_gid;
    unsigned calls;
} CredentialChange;

typedef struct ExitRecorder {
    IProcessLifecycle interface;
    int32_t status;
    unsigned calls;
} ExitRecorder;

static FacetResult record_lifecycle_interface(void *self, uuid_t iid,
                                              FacetHandle *out)
{
    (void)self;
    (void)iid;
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult record_lifecycle_exit(void *self, int32_t status)
{
    ExitRecorder *recorder = self;
    recorder->status = status;
    recorder->calls++;
    return FACET_OK;
}

static FacetResult unused_posix_spawn(void *context, const FacetString *path,
                                      const FacetArray_string *arguments,
                                      FacetHandle session, int32_t *pid,
                                      int32_t *error)
{
    (void)context; (void)path; (void)arguments; (void)session;
    if (pid != NULL) *pid = -1;
    if (error != NULL) *error = ENOSYS;
    return FACET_OK;
}

static FacetResult unused_posix_wait(void *context, int32_t pid,
                                     int32_t *status, int32_t *error)
{
    (void)context; (void)pid;
    if (status != NULL) *status = -1;
    if (error != NULL) *error = ECHILD;
    return FACET_OK;
}

static int record_credential_change(void *context, uint32_t uid, uint32_t gid,
                                    bool set_uid, bool set_gid)
{
    CredentialChange *change = context;
    change->uid = uid;
    change->gid = gid;
    change->set_uid = set_uid;
    change->set_gid = set_gid;
    change->calls++;
    return 0;
}

typedef struct TestStream {
    IByteReader reader;
    IByteWriter writer;
    ITerminalControl control;
    ITerminal terminal;
    FacetHandle reader_handle;
    FacetHandle writer_handle;
    FacetHandle control_handle;
    FacetHandle terminal_handle;
    uint8_t input;
    bool input_ready;
    uint8_t written[32];
    size_t written_count;
} TestStream;

static TestStream test_streams[3];

static FacetResult test_get_reader(void *self, uuid_t iid, FacetHandle *out)
{
    (void)iid;
    *out = ((TestStream *)self)->reader_handle;
    return FACET_OK;
}

static FacetResult test_read(void *self, uint32_t maximum, FacetArray_u8 *out)
{
    TestStream *stream = self;
    static uint8_t returned[3];
    out->data = NULL;
    out->count = 0;
    if (maximum != 0 && stream->input_ready) {
        size_t index = (size_t)(stream - test_streams);
        returned[index] = stream->input;
        out->data = &returned[index];
        out->count = 1;
        stream->input_ready = false;
    }
    return FACET_OK;
}

static FacetResult test_get_writer(void *self, uuid_t iid, FacetHandle *out)
{
    (void)iid;
    *out = ((TestStream *)self)->writer_handle;
    return FACET_OK;
}

static FacetResult test_write(void *self, const FacetArray_u8 *payload,
                              uint32_t *written)
{
    TestStream *stream = self;
    assert(payload->count <= sizeof(stream->written) - stream->written_count);
    memcpy(stream->written + stream->written_count, payload->data,
           payload->count);
    stream->written_count += payload->count;
    *written = (uint32_t)payload->count;
    return FACET_OK;
}

static FacetResult test_get_control(void *self, uuid_t iid, FacetHandle *out)
{
    (void)iid;
    *out = ((TestStream *)self)->control_handle;
    return FACET_OK;
}

static FacetResult test_get_terminal(void *self, uuid_t iid, FacetHandle *out)
{
    (void)iid;
    *out = ((TestStream *)self)->terminal_handle;
    return FACET_OK;
}

static FacetResult test_terminal_input(void *self, FacetHandle *out)
{
    *out = ((TestStream *)self)->reader_handle;
    return FACET_OK;
}

static FacetResult test_terminal_output(void *self, FacetHandle *out)
{
    *out = ((TestStream *)self)->writer_handle;
    return FACET_OK;
}

static FacetResult test_terminal_control(void *self, FacetHandle *out)
{
    *out = ((TestStream *)self)->control_handle;
    return FACET_OK;
}

static void prepare_stream(TestStream *stream)
{
    stream->reader = (IByteReader){stream, stream, test_get_reader, test_read};
    stream->writer = (IByteWriter){stream, stream, test_get_writer, test_write};
    stream->control = (ITerminalControl){stream, stream, test_get_control};
    stream->terminal = (ITerminal){stream, stream, test_get_terminal,
        test_terminal_input, test_terminal_output, test_terminal_control};
    assert(libfacet_export_interface(&stream->reader, &IByteReader_MetaData,
                                     &stream->reader_handle) == FACET_OK);
    assert(libfacet_export_interface(&stream->writer, &IByteWriter_MetaData,
                                     &stream->writer_handle) == FACET_OK);
    assert(libfacet_export_interface(&stream->control,
                                     &ITerminalControl_MetaData,
                                     &stream->control_handle) == FACET_OK);
    assert(libfacet_export_interface(&stream->terminal, &ITerminal_MetaData,
                                     &stream->terminal_handle) == FACET_OK);
}

static void prepare_seats(Dominit0SystemConfig *system)
{
    system->current_seats = calloc(system->parsed.seat_count,
                                   sizeof(*system->current_seats));
    assert(system->current_seats != NULL);
    for (size_t i = 0; i < system->parsed.seat_count; i++) {
        CurrentSeat *seat = &system->current_seats[i];
        seat->config = &system->parsed.seats[i];
        seat->terminals = calloc(seat->config->terminal_count,
                                 sizeof(*seat->terminals));
        assert(seat->terminals != NULL);
    }
    for (size_t i = 0; i < 3; i++) prepare_stream(&test_streams[i]);
    CurrentSeatTerminal *selected[3] = {
        &system->current_seats[0].terminals[0],
        &system->current_seats[1].terminals[0],
        &system->current_seats[1].terminals[2],
    };
    for (size_t i = 0; i < 3; i++) {
        selected[i]->terminal = test_streams[i].terminal_handle;
        selected[i]->input = test_streams[i].reader_handle;
        selected[i]->output = test_streams[i].writer_handle;
        selected[i]->control = test_streams[i].control_handle;
        selected[i]->usable = true;
    }
}

static IProcessEnvironment *make_environment(Dominit0SystemConfig *system,
                                             size_t domain, size_t assignment,
                                             Dominit0ProcessEnvironment **server_out)
{
    Dominit0ProcessEnvironment *server = dominit0_process_environment_create(
        system->current_domains[domain]->environment, (FacetHandle){0}, false,
        true, NULL, (FacetHandle){0}, NULL);
    assert(server != NULL);
    assert(dominit0_process_environment_has_native_capabilities(server));
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
    prepare_seats(&system);
    assert(dominit0_terminal_initialize(&system) == 0);

    Dominit0ProcessEnvironment *serial_server = NULL;
    IProcessEnvironment *serial_environment = make_environment(
        &system, 0, 0, &serial_server);
    IByteReader *serial_input = resolve(serial_environment, "stdin",
                                        &IByteReader_MetaData);
    IByteWriter *serial_output = resolve(serial_environment, "stdout",
                                         &IByteWriter_MetaData);
    test_streams[0].input = 'x';
    test_streams[0].input_ready = true;
    FacetArray_u8 bytes = {0};
    assert(serial_input->read_bytes(serial_input->self, 1, &bytes) == FACET_OK);
    assert(bytes.count == 1 && bytes.data[0] == 'x');
    free(bytes.data);
    FacetArray_u8 output = {.data = (uint8_t *)"ok", .count = 2};
    uint32_t written = 0;
    assert(serial_output->write_bytes(serial_output->self, &output, &written) ==
           FACET_OK);
    assert(written == 2 && test_streams[0].written_count == 2);
    assert(memcmp(test_streams[0].written, "ok", 2) == 0);
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
    assert(test_streams[1].written_count == 1 &&
           test_streams[1].written[0] == 'A');
    test_streams[1].input = 'z';
    test_streams[1].input_ready = true;
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
    assert(test_streams[1].written_count == 1 &&
           test_streams[2].written_count == 1 &&
           test_streams[2].written[0] == 'B');
    bytes = (FacetArray_u8){0};
    assert(tty2_input->read_bytes(tty2_input->self, 1, &bytes) == FACET_OK);
    assert(bytes.count == 0);

    Dominit0ProcessEnvironment *posix_server =
        dominit0_process_environment_create(
            system.current_domains[1]->environment, (FacetHandle){0}, true,
            false, NULL, (FacetHandle){0}, NULL);
    assert(posix_server != NULL);
    assert(!dominit0_process_environment_has_native_capabilities(posix_server));
    assert(dominit0_terminal_bind_process_environment(
               system.current_domains[1], 0, posix_server) == 0);
    CredentialChange credential_change = {0};
    assert(dominit0_process_environment_bind_posix_process_control(
               posix_server, &credential_change, 1, 42, true,
               unused_posix_spawn, unused_posix_wait,
               record_credential_change) == 0);
    ExitRecorder lifecycle = {
        .interface = {.self = &lifecycle, .priv = &lifecycle,
                      .getInterface = record_lifecycle_interface,
                      .notify_exit = record_lifecycle_exit},
    };
    FacetHandle lifecycle_handle = {0};
    assert(libfacet_export_interface(&lifecycle.interface,
                                     &IProcessLifecycle_MetaData,
                                     &lifecycle_handle) == FACET_OK);
    assert(dominit0_process_environment_bind_lifecycle(posix_server,
                                                        lifecycle_handle) == 0);
    FacetHandle posix_root_handle = {0};
    assert(libfacet_handle_clone(
               dominit0_process_environment_root_handle(posix_server),
               &posix_root_handle) == FACET_OK);
    IProcessEnvironment *posix_environment = libfacet_new_proxy_client(
        &IProcessEnvironment_MetaData, posix_root_handle);
    assert(posix_environment != NULL);
    FacetArray_BindingInfo bindings = {0};
    assert(posix_environment->list_bindings(posix_environment->self,
                                            &bindings) == FACET_OK);
    assert(bindings.count == 1 && bindings.data[0].name.length == 5 &&
           memcmp(bindings.data[0].name.data, "posix", 5) == 0);
    assert(dominit0_process_environment_bind_named(
               posix_server, "process.lifecycle", IID_IGenericObject,
               posix_root_handle) != 0);
    FacetString stdout_name = {.data = "stdout", .length = 6};
    FacetHandle hidden = {0};
    assert(posix_environment->resolve(posix_environment->self, &stdout_name,
                                      &hidden) == FACET_NOT_FOUND);
    FacetHandle posix_handle = {0};
    assert(posix_environment->getInterface(posix_environment->self,
                                           IID_IPOSIXView,
                                           &posix_handle) == FACET_OK);
    IPOSIXView *posix = libfacet_new_proxy_client(&IPOSIXView_MetaData,
                                                  posix_handle);
    assert(posix != NULL);
    int32_t credential_error = -1;
    assert(posix->set_credentials(posix->self, 1000, UINT32_MAX,
                                  &credential_error) == FACET_OK);
    assert(credential_error == 0 && credential_change.calls == 1 &&
           credential_change.set_uid && !credential_change.set_gid &&
           credential_change.uid == 1000);
    /* Dropping uid also drops the authority to mutate credentials again. */
    assert(posix->set_credentials(posix->self, 0, UINT32_MAX,
                                  &credential_error) == FACET_OK);
    assert(credential_error == EPERM && credential_change.calls == 1);
    FacetArray_u8 hello = {.data = (uint8_t *)"hello", .count = 5};
    int64_t posix_result = -1;
    int32_t posix_error = -1;
    assert(posix->write_fd(posix->self, 1, &hello, &posix_result,
                           &posix_error) == FACET_OK);
    assert(posix_result == 5 && posix_error == 0);
    assert(test_streams[2].written_count == 6);
    assert(posix->write_fd(posix->self, 1, &hello, &posix_result,
                           &posix_error) == FACET_OK);
    assert(posix_result == 5 && posix_error == 0);
    assert(test_streams[2].written_count == 11);
    assert(posix->write_fd(posix->self, 9, &hello, &posix_result,
                           &posix_error) == FACET_OK);
    assert(posix_result == -1 && posix_error != 0);
    assert(posix->exit_process(posix->self, 23) == FACET_OK);
    assert(lifecycle.calls == 1 && lifecycle.status == 23);
    libfacet_free_proxy_client(posix);
    libfacet_free_proxy_client(posix_environment);
    dominit0_process_environment_destroy(posix_server);
    assert(libfacet_unexport_interface(lifecycle_handle) == FACET_OK);

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
