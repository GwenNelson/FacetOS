#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/terminal.h>

#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/ITerminal.h>
#include <facetos/interfaces/ITerminalControl.h>
#include <facetos/interfaces/ISeat.h>

#include <stdlib.h>
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

#define VGA_TERMINAL_COUNT 5u
#define VGA_COLUMN_COUNT 80u
#define VGA_ROW_COUNT 25u
#define VGA_CELL_COUNT (VGA_COLUMN_COUNT * VGA_ROW_COUNT)
#define VGA_INPUT_CAPACITY 256u

typedef struct Dominit0VgaTerminal {
    IByteReader input;
    IByteWriter output;
    ITerminal terminal;
    ITerminalControl control;
    FacetHandle input_handle;
    FacetHandle output_handle;
    FacetHandle terminal_handle;
    FacetHandle control_handle;
    uint16_t cells[VGA_CELL_COUNT];
    size_t cursor;
    uint8_t input_bytes[VGA_INPUT_CAPACITY];
    size_t input_head;
    size_t input_count;
    size_t index;
} Dominit0VgaTerminal;

typedef struct Dominit0VgaSeat {
    ISeat seat;
    FacetHandle seat_handle;
    Dominit0VgaTerminal terminals[VGA_TERMINAL_COUNT];
    size_t active;
    unsigned int lock;
} Dominit0VgaSeat;

static Dominit0VgaSeat vga_seat;

typedef struct TerminalAssignmentBinding {
    CurrentDomain *domain;
    size_t assignment_index;
    FacetHandle input;
    FacetHandle output;
    FacetHandle control;
    FacetHandle terminal;
} TerminalAssignmentBinding;

static TerminalAssignmentBinding *assignment_bindings;
static size_t assignment_binding_count;

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

static int add_assignment(CurrentDomain *domain, size_t assignment_index,
                          FacetHandle input, FacetHandle output,
                          FacetHandle control, FacetHandle terminal)
{
    TerminalAssignmentBinding *expanded = realloc(
        assignment_bindings,
        (assignment_binding_count + 1) * sizeof(*assignment_bindings));
    if (expanded == NULL) return -1;
    assignment_bindings = expanded;
    TerminalAssignmentBinding *binding =
        &assignment_bindings[assignment_binding_count++];
    *binding = (TerminalAssignmentBinding){
        .domain = domain,
        .assignment_index = assignment_index,
        .input = input,
        .output = output,
        .control = control,
        .terminal = terminal,
    };
    return 0;
}

static int add_serial_assignment(CurrentDomain *domain, size_t assignment_index)
{
    return add_assignment(domain, assignment_index,
                          serial_terminal.input_handle,
                          serial_terminal.output_handle,
                          serial_terminal.control_handle,
                          serial_terminal.terminal_handle);
}

static void vga_lock(void)
{
    while (__atomic_test_and_set(&vga_seat.lock, __ATOMIC_ACQUIRE)) { }
}

static void vga_unlock(void)
{
    __atomic_clear(&vga_seat.lock, __ATOMIC_RELEASE);
}

static void vga_present_active(void)
{
    (void)platform_local_console_present(
        vga_seat.terminals[vga_seat.active].cells, VGA_CELL_COUNT);
}

static void vga_queue_byte(Dominit0VgaTerminal *terminal, uint8_t byte)
{
    if (terminal->input_count == VGA_INPUT_CAPACITY) return;
    size_t tail = (terminal->input_head + terminal->input_count) %
        VGA_INPUT_CAPACITY;
    terminal->input_bytes[tail] = byte;
    terminal->input_count++;
}

static void vga_pump_keyboard(void)
{
    PlatformConsoleKey key;
    while (platform_local_console_poll_key(&key) == 0) {
        if (key.kind == PLATFORM_CONSOLE_KEY_SWITCH_TERMINAL &&
            key.terminal_index < VGA_TERMINAL_COUNT) {
            vga_seat.active = key.terminal_index;
            vga_present_active();
            klog(LOG_DEBUG, "VGA active terminal is tty%zu\n",
                 vga_seat.active + 1);
        } else if (key.kind == PLATFORM_CONSOLE_KEY_BYTE) {
            vga_queue_byte(&vga_seat.terminals[vga_seat.active], key.byte);
        }
    }
}

static FacetResult vga_input_get_interface(void *self, uuid_t iid,
                                           FacetHandle *out)
{
    Dominit0VgaTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IByteReader))
        return return_handle(terminal->input_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult vga_input_read(void *self, uint32_t maximum,
                                  FacetArray_u8 *payload)
{
    Dominit0VgaTerminal *terminal = self;
    static uint8_t returned[VGA_TERMINAL_COUNT];
    if (payload == NULL) return FACET_INVALID_ARGUMENT;
    payload->data = NULL;
    payload->count = 0;
    if (maximum == 0) return FACET_OK;
    vga_lock();
    vga_pump_keyboard();
    if (terminal->input_count != 0) {
        returned[terminal->index] = terminal->input_bytes[terminal->input_head];
        terminal->input_head = (terminal->input_head + 1) % VGA_INPUT_CAPACITY;
        terminal->input_count--;
        payload->data = &returned[terminal->index];
        payload->count = 1;
    }
    vga_unlock();
    return FACET_OK;
}

static FacetResult vga_output_get_interface(void *self, uuid_t iid,
                                            FacetHandle *out)
{
    Dominit0VgaTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IByteWriter))
        return return_handle(terminal->output_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static void vga_scroll(Dominit0VgaTerminal *terminal)
{
    for (size_t i = 0; i < VGA_CELL_COUNT - VGA_COLUMN_COUNT; i++)
        terminal->cells[i] = terminal->cells[i + VGA_COLUMN_COUNT];
    for (size_t i = VGA_CELL_COUNT - VGA_COLUMN_COUNT;
         i < VGA_CELL_COUNT; i++)
        terminal->cells[i] = UINT16_C(0x0720);
    terminal->cursor = VGA_CELL_COUNT - VGA_COLUMN_COUNT;
}

static void vga_put_byte(Dominit0VgaTerminal *terminal, uint8_t byte)
{
    if (byte == '\r') {
        terminal->cursor -= terminal->cursor % VGA_COLUMN_COUNT;
    } else if (byte == '\n') {
        terminal->cursor += VGA_COLUMN_COUNT -
            terminal->cursor % VGA_COLUMN_COUNT;
    } else if (byte == '\b') {
        if (terminal->cursor != 0) terminal->cursor--;
        terminal->cells[terminal->cursor] = UINT16_C(0x0720);
    } else if (byte == '\t') {
        size_t spaces = 8 - (terminal->cursor % 8);
        while (spaces-- != 0) vga_put_byte(terminal, ' ');
    } else if (byte >= 32 && byte < 127) {
        if (terminal->cursor >= VGA_CELL_COUNT) vga_scroll(terminal);
        terminal->cells[terminal->cursor++] =
            (uint16_t)(UINT16_C(0x0700) | byte);
    }
    if (terminal->cursor >= VGA_CELL_COUNT) vga_scroll(terminal);
}

static FacetResult vga_output_write(void *self, const FacetArray_u8 *payload,
                                    uint32_t *written)
{
    Dominit0VgaTerminal *terminal = self;
    if (payload == NULL || written == NULL ||
        (payload->count != 0 && payload->data == NULL) ||
        payload->count > UINT32_MAX)
        return FACET_INVALID_ARGUMENT;
    vga_lock();
    for (size_t i = 0; i < payload->count; i++)
        vga_put_byte(terminal, payload->data[i]);
    if (terminal->index == vga_seat.active) vga_present_active();
    vga_unlock();
    *written = (uint32_t)payload->count;
    return FACET_OK;
}

static FacetResult vga_terminal_get_interface(void *self, uuid_t iid,
                                              FacetHandle *out)
{
    Dominit0VgaTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ITerminal))
        return return_handle(terminal->terminal_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult vga_terminal_input(void *self, FacetHandle *out)
{
    return return_handle(((Dominit0VgaTerminal *)self)->input_handle, out);
}

static FacetResult vga_terminal_output(void *self, FacetHandle *out)
{
    return return_handle(((Dominit0VgaTerminal *)self)->output_handle, out);
}

static FacetResult vga_terminal_control(void *self, FacetHandle *out)
{
    return return_handle(((Dominit0VgaTerminal *)self)->control_handle, out);
}

static FacetResult vga_control_get_interface(void *self, uuid_t iid,
                                             FacetHandle *out)
{
    Dominit0VgaTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_ITerminalControl))
        return return_handle(terminal->control_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult vga_seat_get_interface(void *self, uuid_t iid,
                                          FacetHandle *out)
{
    (void)self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ISeat))
        return return_handle(vga_seat.seat_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult vga_seat_get_terminal(void *self, const FacetString *name,
                                         FacetHandle *out)
{
    (void)self;
    if (name == NULL || name->data == NULL || name->length != 4 ||
        memcmp(name->data, "tty", 3) != 0 || name->data[3] < '1' ||
        name->data[3] > '5')
        return FACET_NOT_FOUND;
    return return_handle(vga_seat.terminals[name->data[3] - '1'].terminal_handle,
                         out);
}

static FacetResult vga_seat_active(void *self, FacetHandle *out)
{
    (void)self;
    return return_handle(vga_seat.terminals[vga_seat.active].terminal_handle,
                         out);
}

static FacetResult vga_seat_set_active(void *self, FacetHandle terminal)
{
    (void)self;
    (void)terminal;
    return FACET_NOT_SUPPORTED;
}

static int vga_terminal_export(void)
{
    if (vga_seat.seat_handle.platform != NULL) return 0;
    if (platform_local_console_initialize() != 0) return -1;
    for (size_t i = 0; i < VGA_TERMINAL_COUNT; i++) {
        Dominit0VgaTerminal *terminal = &vga_seat.terminals[i];
        terminal->index = i;
        terminal->cursor = 0;
        for (size_t cell = 0; cell < VGA_CELL_COUNT; cell++)
            terminal->cells[cell] = UINT16_C(0x0720);
        terminal->input.self = terminal;
        terminal->input.priv = terminal;
        terminal->input.getInterface = vga_input_get_interface;
        terminal->input.read_bytes = vga_input_read;
        terminal->output.self = terminal;
        terminal->output.priv = terminal;
        terminal->output.getInterface = vga_output_get_interface;
        terminal->output.write_bytes = vga_output_write;
        terminal->terminal.self = terminal;
        terminal->terminal.priv = terminal;
        terminal->terminal.getInterface = vga_terminal_get_interface;
        terminal->terminal.get_input = vga_terminal_input;
        terminal->terminal.get_output = vga_terminal_output;
        terminal->terminal.get_control = vga_terminal_control;
        terminal->control.self = terminal;
        terminal->control.priv = terminal;
        terminal->control.getInterface = vga_control_get_interface;
        if (libfacet_export_interface(&terminal->input, &IByteReader_MetaData,
                                      &terminal->input_handle) != FACET_OK ||
            libfacet_export_interface(&terminal->output, &IByteWriter_MetaData,
                                      &terminal->output_handle) != FACET_OK ||
            libfacet_export_interface(&terminal->control,
                                      &ITerminalControl_MetaData,
                                      &terminal->control_handle) != FACET_OK ||
            libfacet_export_interface(&terminal->terminal, &ITerminal_MetaData,
                                      &terminal->terminal_handle) != FACET_OK)
            return -1;
    }
    vga_seat.seat.self = &vga_seat;
    vga_seat.seat.priv = &vga_seat;
    vga_seat.seat.getInterface = vga_seat_get_interface;
    vga_seat.seat.get_terminal = vga_seat_get_terminal;
    vga_seat.seat.get_active_terminal = vga_seat_active;
    vga_seat.seat.set_active_terminal = vga_seat_set_active;
    if (libfacet_export_interface(&vga_seat.seat, &ISeat_MetaData,
                                  &vga_seat.seat_handle) != FACET_OK)
        return -1;
    vga_present_active();
    return 0;
}

int dominit0_terminal_initialize(Dominit0SystemConfig *system)
{
    if (system == NULL || system->current_domains == NULL)
        return -1;
    bool serial_is_assigned = false;
    bool vga_is_assigned = false;
    for (size_t domain_index = 0; domain_index < system->domain_count; domain_index++) {
        const FacetConfigDomain *domain = &system->parsed.domains[domain_index];
        for (size_t terminal_index = 0; terminal_index < domain->terminal_count; terminal_index++) {
            const FacetConfigTerminalAssignment *assignment =
                &domain->terminals[terminal_index];
            FacetConfigSeatType type =
                system->parsed.seats[assignment->seat_index].type;
            if (type == FACET_CONFIG_SEAT_SERIAL) {
                if (!serial_is_assigned && serial_terminal_export() != 0)
                    return -1;
                serial_is_assigned = true;
                if (add_serial_assignment(system->current_domains[domain_index],
                                          terminal_index) != 0)
                    return -1;
                klog(LOG_INFO, "Prepared serial terminal %s for domain %llu\n",
                     assignment->reference, (unsigned long long)domain->id);
            } else if (type == FACET_CONFIG_SEAT_LOCAL) {
                if (!vga_is_assigned && vga_terminal_export() != 0)
                    return -1;
                vga_is_assigned = true;
                size_t local_index = assignment->terminal_index;
                if (local_index >= VGA_TERMINAL_COUNT)
                    return -1;
                Dominit0VgaTerminal *terminal =
                    &vga_seat.terminals[local_index];
                if (add_assignment(system->current_domains[domain_index],
                                   terminal_index, terminal->input_handle,
                                   terminal->output_handle,
                                   terminal->control_handle,
                                   terminal->terminal_handle) != 0)
                    return -1;
                klog(LOG_INFO, "Prepared VGA terminal %s for domain %llu\n",
                     assignment->reference, (unsigned long long)domain->id);
            }
        }
    }
    return 0;
}

int dominit0_terminal_bind_process_environment(
    CurrentDomain *domain, size_t assignment_index,
    Dominit0ProcessEnvironment *environment)
{
    if (domain == NULL || environment == NULL) return -1;
    for (size_t i = 0; i < assignment_binding_count; i++) {
        TerminalAssignmentBinding *binding = &assignment_bindings[i];
        if (binding->domain != domain ||
            binding->assignment_index != assignment_index)
            continue;
        return dominit0_process_environment_bind_named(
                   environment, "terminal.control", IID_ITerminalControl,
                   binding->control) ||
               dominit0_process_environment_bind_named(
                   environment, "stdin", IID_IByteReader, binding->input) ||
               dominit0_process_environment_bind_named(
                   environment, "stdout", IID_IByteWriter, binding->output) ||
               dominit0_process_environment_bind_named(
                   environment, "stderr", IID_IByteWriter, binding->output);
    }
    return -1;
}

void dominit0_terminal_destroy(void)
{
    free(assignment_bindings);
    assignment_bindings = NULL;
    assignment_binding_count = 0;
    if (vga_seat.seat_handle.platform != NULL)
        (void)libfacet_unexport_interface(vga_seat.seat_handle);
    for (size_t i = 0; i < VGA_TERMINAL_COUNT; i++) {
        Dominit0VgaTerminal *terminal = &vga_seat.terminals[i];
        if (terminal->terminal_handle.platform != NULL)
            (void)libfacet_unexport_interface(terminal->terminal_handle);
        if (terminal->control_handle.platform != NULL)
            (void)libfacet_unexport_interface(terminal->control_handle);
        if (terminal->output_handle.platform != NULL)
            (void)libfacet_unexport_interface(terminal->output_handle);
        if (terminal->input_handle.platform != NULL)
            (void)libfacet_unexport_interface(terminal->input_handle);
    }
    memset(&vga_seat, 0, sizeof(vga_seat));
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
