#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/terminal.h>

#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/ITerminal.h>
#include <facetos/interfaces/ITerminalControl.h>
#include <facetos/interfaces/ISeat.h>

#include <string.h>

typedef struct Dominit0SerialTerminal {
    IByteReader input;
    IByteWriter output;
    ITerminal terminal;
    ITerminalControl control;
    ISeat seat;
    FacetHandle input_handle;
    FacetHandle output_handle;
    FacetHandle terminal_handle;
    FacetHandle control_handle;
    FacetHandle seat_handle;
} Dominit0SerialTerminal;

static Dominit0SerialTerminal serial_terminal;

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult return_handle(FacetHandle handle, FacetHandle *result)
{
    if (result == NULL)
        return FACET_INVALID_ARGUMENT;
    *result = (FacetHandle){0};
    if (handle.platform == NULL)
        return FACET_INVALID_HANDLE;
    *result = handle;
    return FACET_OK;
}

static FacetResult input_get_interface(void *self, uuid_t iid,
                                       FacetHandle *result)
{
    (void)self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IByteReader))
        return return_handle(serial_terminal.input_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult input_read_bytes(void *self, uint32_t maximum,
                                    FacetArray_u8 *payload)
{
    (void)self;
    static uint8_t byte;
    if (payload == NULL)
        return FACET_INVALID_ARGUMENT;
    payload->data = NULL;
    payload->count = 0;
    if (maximum == 0)
        return FACET_OK;
    int status = platform_serial_read_byte(&byte);
    if (status < 0)
        return FACET_ERROR;
    if (status == 0) {
        payload->data = &byte;
        payload->count = 1;
    }
    return FACET_OK;
}

static FacetResult output_get_interface(void *self, uuid_t iid,
                                        FacetHandle *result)
{
    (void)self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IByteWriter))
        return return_handle(serial_terminal.output_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult output_write_bytes(void *self, const FacetArray_u8 *payload,
                                      uint32_t *written)
{
    (void)self;
    if (written == NULL || payload == NULL ||
        (payload->count != 0 && payload->data == NULL))
        return FACET_INVALID_ARGUMENT;
    *written = 0;
    if (payload->count > UINT32_MAX)
        return FACET_BUFFER_TOO_SMALL;
    if (platform_serial_write(payload->data, payload->count) != 0)
        return FACET_ERROR;
    *written = (uint32_t)payload->count;
    return FACET_OK;
}

static FacetResult terminal_get_interface(void *self, uuid_t iid,
                                          FacetHandle *result)
{
    (void)self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ITerminal))
        return return_handle(serial_terminal.terminal_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult terminal_get_input(void *self, FacetHandle *input)
{
    (void)self;
    return return_handle(serial_terminal.input_handle, input);
}

static FacetResult terminal_get_output(void *self, FacetHandle *output)
{
    (void)self;
    return return_handle(serial_terminal.output_handle, output);
}

static FacetResult terminal_get_control(void *self, FacetHandle *control)
{
    (void)self;
    return return_handle(serial_terminal.control_handle, control);
}

static FacetResult control_get_interface(void *self, uuid_t iid,
                                         FacetHandle *result)
{
    (void)self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ITerminalControl))
        return return_handle(serial_terminal.control_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult seat_get_interface(void *self, uuid_t iid, FacetHandle *result)
{
    (void)self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ISeat))
        return return_handle(serial_terminal.seat_handle, result);
    if (result != NULL) *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult seat_get_terminal(void *self, const FacetString *name,
                                     FacetHandle *terminal)
{
    (void)self;
    static const char terminal_name[] = "ttyS0";
    if (name == NULL || name->data == NULL ||
        name->length != sizeof(terminal_name) - 1 ||
        memcmp(name->data, terminal_name, sizeof(terminal_name) - 1) != 0)
        return FACET_NOT_FOUND;
    return return_handle(serial_terminal.terminal_handle, terminal);
}

static FacetResult seat_get_active_terminal(void *self, FacetHandle *terminal)
{
    (void)self;
    return return_handle(serial_terminal.terminal_handle, terminal);
}

static FacetResult seat_set_active_terminal(void *self, FacetHandle terminal)
{
    (void)self;
    (void)terminal;
    /* A serial seat has one immutable active terminal.  Multi-terminal
     * selection belongs to the later VGA/PS2 seat backend. */
    return FACET_NOT_SUPPORTED;
}

static int serial_terminal_export(void)
{
    if (serial_terminal.terminal_handle.platform != NULL)
        return 0;
    serial_terminal.input.self = &serial_terminal;
    serial_terminal.input.priv = &serial_terminal;
    serial_terminal.input.getInterface = input_get_interface;
    serial_terminal.input.read_bytes = input_read_bytes;
    serial_terminal.output.self = &serial_terminal;
    serial_terminal.output.priv = &serial_terminal;
    serial_terminal.output.getInterface = output_get_interface;
    serial_terminal.output.write_bytes = output_write_bytes;
    serial_terminal.terminal.self = &serial_terminal;
    serial_terminal.terminal.priv = &serial_terminal;
    serial_terminal.terminal.getInterface = terminal_get_interface;
    serial_terminal.terminal.get_input = terminal_get_input;
    serial_terminal.terminal.get_output = terminal_get_output;
    serial_terminal.terminal.get_control = terminal_get_control;
    serial_terminal.control.self = &serial_terminal;
    serial_terminal.control.priv = &serial_terminal;
    serial_terminal.control.getInterface = control_get_interface;
    serial_terminal.seat.self = &serial_terminal;
    serial_terminal.seat.priv = &serial_terminal;
    serial_terminal.seat.getInterface = seat_get_interface;
    serial_terminal.seat.get_terminal = seat_get_terminal;
    serial_terminal.seat.get_active_terminal = seat_get_active_terminal;
    serial_terminal.seat.set_active_terminal = seat_set_active_terminal;
    if (libfacet_export_interface(&serial_terminal.input, &IByteReader_MetaData,
                                  &serial_terminal.input_handle) != FACET_OK ||
        libfacet_export_interface(&serial_terminal.output, &IByteWriter_MetaData,
                                  &serial_terminal.output_handle) != FACET_OK ||
        libfacet_export_interface(&serial_terminal.control, &ITerminalControl_MetaData,
                                  &serial_terminal.control_handle) != FACET_OK ||
        libfacet_export_interface(&serial_terminal.terminal, &ITerminal_MetaData,
                                  &serial_terminal.terminal_handle) != FACET_OK ||
        libfacet_export_interface(&serial_terminal.seat, &ISeat_MetaData,
                                  &serial_terminal.seat_handle) != FACET_OK) {
        dominit0_terminal_destroy();
        return -1;
    }
    return 0;
}

static int bind_serial_terminal(Dominit0DomainEnvironment *environment)
{
    return dominit0_environment_bind_named(environment, "terminal", IID_ITerminal,
                                           serial_terminal.terminal_handle) ||
           dominit0_environment_bind_named(environment, "terminal.input", IID_IByteReader,
                                           serial_terminal.input_handle) ||
           dominit0_environment_bind_named(environment, "terminal.output", IID_IByteWriter,
                                           serial_terminal.output_handle) ||
           dominit0_environment_bind_named(environment, "terminal.control", IID_ITerminalControl,
                                           serial_terminal.control_handle) ||
           dominit0_environment_bind_named(environment, "seat", IID_ISeat,
                                           serial_terminal.seat_handle) ||
           dominit0_environment_bind_named(environment, "stdin", IID_IByteReader,
                                           serial_terminal.input_handle) ||
           dominit0_environment_bind_named(environment, "stdout", IID_IByteWriter,
                                           serial_terminal.output_handle) ||
           dominit0_environment_bind_named(environment, "stderr", IID_IByteWriter,
                                           serial_terminal.output_handle);
}

int dominit0_terminal_initialize(Dominit0SystemConfig *system)
{
    if (system == NULL || system->current_domains == NULL)
        return -1;
    bool serial_is_assigned = false;
    for (size_t domain_index = 0; domain_index < system->domain_count; domain_index++) {
        const FacetConfigDomain *domain = &system->parsed.domains[domain_index];
        for (size_t terminal_index = 0; terminal_index < domain->terminal_count; terminal_index++) {
            const FacetConfigTerminalAssignment *assignment =
                &domain->terminals[terminal_index];
            if (system->parsed.seats[assignment->seat_index].type !=
                FACET_CONFIG_SEAT_SERIAL)
                continue;
            if (!serial_is_assigned && serial_terminal_export() != 0)
                return -1;
            serial_is_assigned = true;
            if (bind_serial_terminal(system->current_domains[domain_index]->environment) != 0)
                return -1;
            klog(LOG_INFO, "Delegated serial terminal %s to domain %llu\n",
                 assignment->reference, (unsigned long long)domain->id);
        }
    }
    return 0;
}

void dominit0_terminal_destroy(void)
{
    if (serial_terminal.terminal_handle.platform != NULL)
        (void)libfacet_unexport_interface(serial_terminal.terminal_handle);
    if (serial_terminal.seat_handle.platform != NULL)
        (void)libfacet_unexport_interface(serial_terminal.seat_handle);
    if (serial_terminal.control_handle.platform != NULL)
        (void)libfacet_unexport_interface(serial_terminal.control_handle);
    if (serial_terminal.output_handle.platform != NULL)
        (void)libfacet_unexport_interface(serial_terminal.output_handle);
    if (serial_terminal.input_handle.platform != NULL)
        (void)libfacet_unexport_interface(serial_terminal.input_handle);
    memset(&serial_terminal, 0, sizeof(serial_terminal));
}
