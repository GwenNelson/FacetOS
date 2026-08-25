#include "../src/seat/pc_console.h"
#include "../src/seat/pc_cursor.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Operation {
    char kind;
    uint16_t port;
    uint8_t value;
} Operation;

typedef struct MockHardware {
    Operation operations[64];
    size_t operation_count;
    uint8_t cursor_start;
    size_t presents;
    size_t last_cursor;
} MockHardware;

static void record(MockHardware *hardware, char kind, uint16_t port,
                   uint8_t value)
{
    assert(hardware->operation_count <
           sizeof(hardware->operations) / sizeof(hardware->operations[0]));
    hardware->operations[hardware->operation_count++] =
        (Operation){kind, port, value};
}

static int port_read(void *context, uint16_t port, uint8_t *value)
{
    MockHardware *hardware = context;
    assert(port == 0x3d5);
    *value = hardware->cursor_start;
    record(hardware, 'r', port, *value);
    return 0;
}

static int port_write(void *context, uint16_t port, uint8_t value)
{
    record(context, 'w', port, value);
    return 0;
}

static SeatPCCursorIO cursor_io(MockHardware *hardware)
{
    return (SeatPCCursorIO){
        .read = port_read,
        .write = port_write,
        .context = hardware,
    };
}

static void assert_cursor_write(const MockHardware *hardware, size_t first,
                                size_t cursor)
{
    const Operation expected[] = {
        {'w', 0x3d4, 0x0f}, {'w', 0x3d5, (uint8_t)cursor},
        {'w', 0x3d4, 0x0e}, {'w', 0x3d5, (uint8_t)(cursor >> 8)},
    };
    assert(hardware->operation_count == first + 4);
    for (size_t i = 0; i < 4; i++) {
        assert(hardware->operations[first + i].kind == expected[i].kind);
        assert(hardware->operations[first + i].port == expected[i].port);
        assert(hardware->operations[first + i].value == expected[i].value);
    }
}

static int present(void *context, const uint16_t *cells, size_t count,
                   size_t cursor)
{
    MockHardware *hardware = context;
    assert(cells != NULL);
    assert(count == SEAT_PC_CELLS);
    hardware->presents++;
    hardware->last_cursor = cursor;
    SeatPCCursorIO io = cursor_io(hardware);
    return seat_pc_cursor_set(&io, (uint16_t)cursor);
}

int main(void)
{
    MockHardware hardware = {.cursor_start = 0x2d};
    SeatPCCursorIO io = cursor_io(&hardware);

    assert(seat_pc_cursor_enable(&io) == 0);
    const Operation enabled[] = {
        {'w', 0x3d4, 0x0a}, {'r', 0x3d5, 0x2d},
        {'w', 0x3d4, 0x0a}, {'w', 0x3d5, 0x0d},
    };
    assert(hardware.operation_count == 4);
    for (size_t i = 0; i < 4; i++) {
        assert(hardware.operations[i].kind == enabled[i].kind);
        assert(hardware.operations[i].port == enabled[i].port);
        assert(hardware.operations[i].value == enabled[i].value);
    }

    SeatPCConsole console;
    seat_pc_console_initialize(&console, present, &hardware);

    size_t first = hardware.operation_count;
    assert(seat_pc_console_write(&console, 0,
                                 (const uint8_t *)"abc", 3) == 0);
    assert(hardware.last_cursor == 3);
    assert_cursor_write(&hardware, first, 3);

    first = hardware.operation_count;
    assert(seat_pc_console_write(&console, 0,
                                 (const uint8_t *)"\n", 1) == 0);
    assert(hardware.last_cursor == 80);
    assert_cursor_write(&hardware, first, 80);

    first = hardware.operation_count;
    assert(seat_pc_console_write(&console, 0,
                                 (const uint8_t *)"\b", 1) == 0);
    assert(hardware.last_cursor == 79);
    assert_cursor_write(&hardware, first, 79);

    first = hardware.operation_count;
    size_t presents = hardware.presents;
    assert(seat_pc_console_write(&console, 1,
                                 (const uint8_t *)"x", 1) == 0);
    assert(hardware.operation_count == first);
    assert(hardware.presents == presents);

    assert(seat_pc_console_select(&console, 1) == 0);
    assert(hardware.last_cursor == 1);
    assert_cursor_write(&hardware, first, 1);

    uint8_t lines[SEAT_PC_CELLS];
    memset(lines, 'x', sizeof(lines));
    assert(seat_pc_console_select(&console, 2) == 0);
    first = hardware.operation_count;
    assert(seat_pc_console_write(&console, 2, lines, sizeof(lines)) == 0);
    assert(hardware.last_cursor == SEAT_PC_CELLS - SEAT_PC_COLUMNS);
    assert_cursor_write(&hardware, first,
                        SEAT_PC_CELLS - SEAT_PC_COLUMNS);

    puts("seat PC console tests passed");
    return 0;
}
