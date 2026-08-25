#include "pc_console.h"

#include <string.h>

static void scroll_screen(SeatPCScreen *screen)
{
    memmove(screen->cells, screen->cells + SEAT_PC_COLUMNS,
            (SEAT_PC_CELLS - SEAT_PC_COLUMNS) * sizeof(*screen->cells));
    for (size_t i = SEAT_PC_CELLS - SEAT_PC_COLUMNS;
         i < SEAT_PC_CELLS; i++)
        screen->cells[i] = UINT16_C(0x0720);
    screen->cursor = SEAT_PC_CELLS - SEAT_PC_COLUMNS;
}

static void put_byte(SeatPCScreen *screen, uint8_t byte)
{
    if (byte == '\r') {
        screen->cursor -= screen->cursor % SEAT_PC_COLUMNS;
    } else if (byte == '\n') {
        screen->cursor += SEAT_PC_COLUMNS - screen->cursor % SEAT_PC_COLUMNS;
    } else if (byte == '\b') {
        if (screen->cursor != 0) screen->cursor--;
        screen->cells[screen->cursor] = UINT16_C(0x0720);
    } else if (byte == '\t') {
        size_t spaces = 8 - screen->cursor % 8;
        while (spaces-- != 0) put_byte(screen, ' ');
    } else if (byte >= 32 && byte < 127) {
        if (screen->cursor >= SEAT_PC_CELLS) scroll_screen(screen);
        screen->cells[screen->cursor++] =
            (uint16_t)(UINT16_C(0x0700) | byte);
    }
    if (screen->cursor >= SEAT_PC_CELLS) scroll_screen(screen);
}

void seat_pc_console_initialize(SeatPCConsole *console,
                                SeatPCPresent present, void *context)
{
    if (console == NULL) return;
    memset(console, 0, sizeof(*console));
    console->present = present;
    console->present_context = context;
    for (size_t terminal = 0; terminal < SEAT_PC_TERMINALS; terminal++)
        for (size_t cell = 0; cell < SEAT_PC_CELLS; cell++)
            console->screens[terminal].cells[cell] = UINT16_C(0x0720);
}

int seat_pc_console_present(SeatPCConsole *console)
{
    if (console == NULL || console->present == NULL ||
        console->active >= SEAT_PC_TERMINALS)
        return -1;
    SeatPCScreen *screen = &console->screens[console->active];
    return console->present(console->present_context, screen->cells,
                            SEAT_PC_CELLS, screen->cursor);
}

int seat_pc_console_select(SeatPCConsole *console, size_t terminal)
{
    if (console == NULL || terminal >= SEAT_PC_TERMINALS)
        return -1;
    console->active = terminal;
    return seat_pc_console_present(console);
}

int seat_pc_console_write(SeatPCConsole *console, size_t terminal,
                          const uint8_t *data, size_t size)
{
    if (console == NULL || terminal >= SEAT_PC_TERMINALS ||
        (data == NULL && size != 0))
        return -1;
    for (size_t i = 0; i < size; i++)
        put_byte(&console->screens[terminal], data[i]);
    return terminal == console->active ? seat_pc_console_present(console) : 0;
}
