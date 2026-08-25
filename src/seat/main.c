#include "platform/api.h"
#include "pc_console.h"

#include <platform/allocator.h>

#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/ISeat.h>
#include <facetos/interfaces/ITerminal.h>
#include <facetos/interfaces/ITerminalControl.h>
#include <facetos/libfacet/common.h>
#include <facetos/libfacet/platform/sel4/service.h>

#include <sel4/sel4.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_CAPACITY 256u

typedef struct SeatTerminal SeatTerminal;

typedef struct SeatState {
    ISeat interface;
    FacetHandle handle;
    SeatTerminal *terminals;
    size_t terminal_count;
    size_t active;
#if defined(FACET_SEAT_PC)
    SeatPCConsole console;
#endif
} SeatState;

struct SeatTerminal {
    IByteReader input;
    IByteWriter output;
    ITerminal terminal;
    ITerminalControl control;
    FacetHandle input_handle;
    FacetHandle output_handle;
    FacetHandle terminal_handle;
    FacetHandle control_handle;
    SeatState *seat;
    const char *name;
    size_t index;
    uint8_t input_bytes[INPUT_CAPACITY];
    size_t input_head;
    size_t input_count;
};

static SeatState seat;
static SeatTerminal terminals[SEAT_PC_TERMINALS];

#if defined(FACET_SEAT_PC)
static int platform_present(void *context, const uint16_t *cells,
                            size_t count, size_t cursor)
{
    (void)context;
    return seat_platform_present(cells, count, cursor);
}
#endif

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult return_handle(FacetHandle handle, FacetHandle *out)
{
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (handle.platform == NULL) return FACET_INVALID_HANDLE;
    *out = handle;
    return FACET_OK;
}

static FacetResult input_get_interface(void *self, uuid_t iid,
                                       FacetHandle *out)
{
    SeatTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IByteReader))
        return return_handle(terminal->input_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static void queue_byte(SeatTerminal *terminal, uint8_t byte)
{
    if (terminal->input_count == INPUT_CAPACITY) return;
    size_t tail = (terminal->input_head + terminal->input_count) %
        INPUT_CAPACITY;
    terminal->input_bytes[tail] = byte;
    terminal->input_count++;
}

static void present_active(void)
{
#if defined(FACET_SEAT_PC)
    (void)seat_pc_console_present(&seat.console);
#endif
}

static void pump_keyboard(void)
{
#if defined(FACET_SEAT_PC)
    SeatKey key;
    while (seat_platform_poll_key(&key) == 0) {
        if (key.kind == SEAT_KEY_SWITCH_TERMINAL &&
            key.terminal_index < seat.terminal_count) {
            seat.active = key.terminal_index;
            (void)seat_pc_console_select(&seat.console, seat.active);
        } else if (key.kind == SEAT_KEY_BYTE) {
            queue_byte(&seat.terminals[seat.active], key.byte);
        }
    }
#endif
}

static FacetResult input_read(void *self, uint32_t maximum,
                              FacetArray_u8 *payload)
{
    SeatTerminal *terminal = self;
    static uint8_t returned[SEAT_PC_TERMINALS];
    if (payload == NULL) return FACET_INVALID_ARGUMENT;
    payload->data = NULL;
    payload->count = 0;
    if (maximum == 0) return FACET_OK;
#if defined(FACET_SEAT_SERIAL)
    int status = seat_platform_read_byte(&returned[0]);
    if (status < 0) return FACET_ERROR;
    if (status == 0) {
        payload->data = &returned[0];
        payload->count = 1;
    }
#else
    pump_keyboard();
    if (terminal->input_count != 0) {
        returned[terminal->index] = terminal->input_bytes[terminal->input_head];
        terminal->input_head = (terminal->input_head + 1) % INPUT_CAPACITY;
        terminal->input_count--;
        payload->data = &returned[terminal->index];
        payload->count = 1;
    }
#endif
    return FACET_OK;
}

static FacetResult output_get_interface(void *self, uuid_t iid,
                                        FacetHandle *out)
{
    SeatTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_IByteWriter))
        return return_handle(terminal->output_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult output_write(void *self, const FacetArray_u8 *payload,
                                uint32_t *written)
{
    SeatTerminal *terminal = self;
    if (payload == NULL || written == NULL ||
        (payload->count != 0 && payload->data == NULL) ||
        payload->count > UINT32_MAX)
        return FACET_INVALID_ARGUMENT;
#if defined(FACET_SEAT_SERIAL)
    if (seat_platform_write(payload->data, payload->count) != 0)
        return FACET_ERROR;
#else
    if (seat_pc_console_write(&seat.console, terminal->index,
                              payload->data, payload->count) != 0)
        return FACET_ERROR;
#endif
    *written = (uint32_t)payload->count;
    return FACET_OK;
}

static FacetResult terminal_get_interface(void *self, uuid_t iid,
                                          FacetHandle *out)
{
    SeatTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ITerminal))
        return return_handle(terminal->terminal_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult terminal_input(void *self, FacetHandle *out)
{
    return return_handle(((SeatTerminal *)self)->input_handle, out);
}

static FacetResult terminal_output(void *self, FacetHandle *out)
{
    return return_handle(((SeatTerminal *)self)->output_handle, out);
}

static FacetResult terminal_control(void *self, FacetHandle *out)
{
    return return_handle(((SeatTerminal *)self)->control_handle, out);
}

static FacetResult control_get_interface(void *self, uuid_t iid,
                                         FacetHandle *out)
{
    SeatTerminal *terminal = self;
    if (iid_equal(iid, IID_IGenericObject) ||
        iid_equal(iid, IID_ITerminalControl))
        return return_handle(terminal->control_handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult seat_get_interface(void *self, uuid_t iid,
                                      FacetHandle *out)
{
    SeatState *state = self;
    if (iid_equal(iid, IID_IGenericObject) || iid_equal(iid, IID_ISeat))
        return return_handle(state->handle, out);
    if (out != NULL) *out = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult seat_get_terminal(void *self, const FacetString *name,
                                     FacetHandle *out)
{
    SeatState *state = self;
    if (name == NULL || name->data == NULL) return FACET_INVALID_ARGUMENT;
    for (size_t i = 0; i < state->terminal_count; i++) {
        size_t length = strlen(state->terminals[i].name);
        if (name->length == length &&
            memcmp(name->data, state->terminals[i].name, length) == 0)
            return return_handle(state->terminals[i].terminal_handle, out);
    }
    return FACET_NOT_FOUND;
}

static FacetResult seat_active(void *self, FacetHandle *out)
{
    SeatState *state = self;
    return return_handle(state->terminals[state->active].terminal_handle, out);
}

static FacetResult seat_set_active(void *self, FacetHandle terminal)
{
    (void)self; (void)terminal;
    return FACET_NOT_SUPPORTED;
}

static int export_terminal(SeatTerminal *terminal)
{
    terminal->input = (IByteReader){
        .self = terminal, .priv = terminal,
        .getInterface = input_get_interface, .read_bytes = input_read,
    };
    terminal->output = (IByteWriter){
        .self = terminal, .priv = terminal,
        .getInterface = output_get_interface, .write_bytes = output_write,
    };
    terminal->terminal = (ITerminal){
        .self = terminal, .priv = terminal,
        .getInterface = terminal_get_interface,
        .get_input = terminal_input, .get_output = terminal_output,
        .get_control = terminal_control,
    };
    terminal->control = (ITerminalControl){
        .self = terminal, .priv = terminal,
        .getInterface = control_get_interface,
    };
    return libfacet_export_interface(&terminal->input, &IByteReader_MetaData,
                                     &terminal->input_handle) != FACET_OK ||
           libfacet_export_interface(&terminal->output, &IByteWriter_MetaData,
                                     &terminal->output_handle) != FACET_OK ||
           libfacet_export_interface(&terminal->control,
                                     &ITerminalControl_MetaData,
                                     &terminal->control_handle) != FACET_OK ||
           libfacet_export_interface(&terminal->terminal, &ITerminal_MetaData,
                                     &terminal->terminal_handle) != FACET_OK;
}

static int export_seat(void)
{
    static const char *pc_names[SEAT_PC_TERMINALS] = {
        "tty1", "tty2", "tty3", "tty4", "tty5",
    };
    seat.terminals = terminals;
#if defined(FACET_SEAT_SERIAL)
    seat.terminal_count = 1;
    terminals[0].name = "ttyS0";
#else
    seat.terminal_count = SEAT_PC_TERMINALS;
    seat_pc_console_initialize(&seat.console, platform_present, NULL);
    for (size_t i = 0; i < SEAT_PC_TERMINALS; i++) terminals[i].name = pc_names[i];
#endif
    for (size_t i = 0; i < seat.terminal_count; i++) {
        terminals[i].seat = &seat;
        terminals[i].index = i;
        if (export_terminal(&terminals[i]) != 0) return -1;
    }
    seat.interface = (ISeat){
        .self = &seat, .priv = &seat,
        .getInterface = seat_get_interface,
        .get_terminal = seat_get_terminal,
        .get_active_terminal = seat_active,
        .set_active_terminal = seat_set_active,
    };
    if (libfacet_export_interface(&seat.interface, &ISeat_MetaData,
                                  &seat.handle) != FACET_OK)
        return -1;
    present_active();
    return 0;
}

static int parse_word(const char *text, uint64_t *out)
{
    if (text == NULL || out == NULL || *text == '\0') return -1;
    uint64_t value = 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9' ||
            value > (UINT64_MAX - (uint64_t)(*text - '0')) / 10)
            return -1;
        value = value * 10 + (uint64_t)(*text++ - '0');
    }
    *out = value;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 14 ||
        libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteReader_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ITerminalControl_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ITerminal_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&ISeat_MetaData) != FACET_OK)
        return 1;
    uint64_t service_endpoint, ready_endpoint, device0, device1;
    uint64_t receive_slot, export_slot, cnode, depth, vga_address;
    if (parse_word(argv[5], &service_endpoint) != 0 ||
        parse_word(argv[6], &ready_endpoint) != 0 ||
        parse_word(argv[7], &device0) != 0 ||
        parse_word(argv[8], &device1) != 0 ||
        parse_word(argv[9], &receive_slot) != 0 ||
        parse_word(argv[10], &export_slot) != 0 ||
        parse_word(argv[11], &cnode) != 0 ||
        parse_word(argv[12], &depth) != 0 ||
        parse_word(argv[13], &vga_address) != 0 ||
        facet_sel4_service_init(service_endpoint, cnode, receive_slot,
                                export_slot, depth) != FACET_OK ||
        seat_platform_initialize(device0, device1, vga_address) != 0 ||
        facet_app_allocator_use_static() != 0 ||
        export_seat() != 0)
        return 1;
    seL4_CPtr seat_cap;
    if (facet_sel4_service_handle_cap(seat.handle, &seat_cap) != FACET_OK)
        return 1;
    seL4_SetCap(0, seat_cap);
    seL4_MessageInfo_t ready = seL4_MessageInfo_new(0, 0, 1, 0);
    (void)seL4_Call((seL4_CPtr)ready_endpoint, ready);
    return facet_sel4_service_run() == FACET_OK ? 0 : 1;
}
