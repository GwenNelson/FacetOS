#pragma once

#include <stddef.h>
#include <stdint.h>

#define SEAT_PC_TERMINALS 5u
#define SEAT_PC_COLUMNS 80u
#define SEAT_PC_ROWS 25u
#define SEAT_PC_CELLS (SEAT_PC_COLUMNS * SEAT_PC_ROWS)

typedef int (*SeatPCPresent)(void *context, const uint16_t *cells,
                             size_t count, size_t cursor);

typedef struct SeatPCScreen {
    uint16_t cells[SEAT_PC_CELLS];
    size_t cursor;
} SeatPCScreen;

typedef struct SeatPCConsole {
    SeatPCScreen screens[SEAT_PC_TERMINALS];
    size_t active;
    SeatPCPresent present;
    void *present_context;
} SeatPCConsole;

void seat_pc_console_initialize(SeatPCConsole *console,
                                SeatPCPresent present, void *context);
int seat_pc_console_present(SeatPCConsole *console);
int seat_pc_console_select(SeatPCConsole *console, size_t terminal);
int seat_pc_console_write(SeatPCConsole *console, size_t terminal,
                          const uint8_t *data, size_t size);
