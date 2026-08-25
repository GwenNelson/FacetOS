#include "pc_cursor.h"

#include <stddef.h>

enum {
    VGA_CRTC_INDEX = 0x3d4,
    VGA_CRTC_DATA = 0x3d5,
    VGA_CURSOR_START = 0x0a,
    VGA_CURSOR_HIGH = 0x0e,
    VGA_CURSOR_LOW = 0x0f,
    VGA_CURSOR_DISABLED = 0x20,
};

static int read_register(const SeatPCCursorIO *io, uint8_t index,
                         uint8_t *value)
{
    if (io == NULL || io->read == NULL || io->write == NULL || value == NULL)
        return -1;
    if (io->write(io->context, VGA_CRTC_INDEX, index) != 0)
        return -1;
    return io->read(io->context, VGA_CRTC_DATA, value);
}

static int write_register(const SeatPCCursorIO *io, uint8_t index,
                          uint8_t value)
{
    if (io == NULL || io->write == NULL)
        return -1;
    if (io->write(io->context, VGA_CRTC_INDEX, index) != 0)
        return -1;
    return io->write(io->context, VGA_CRTC_DATA, value);
}

int seat_pc_cursor_enable(const SeatPCCursorIO *io)
{
    uint8_t start;
    if (read_register(io, VGA_CURSOR_START, &start) != 0)
        return -1;
    return write_register(io, VGA_CURSOR_START,
                          (uint8_t)(start & ~VGA_CURSOR_DISABLED));
}

int seat_pc_cursor_set(const SeatPCCursorIO *io, uint16_t cell)
{
    if (write_register(io, VGA_CURSOR_LOW, (uint8_t)cell) != 0)
        return -1;
    return write_register(io, VGA_CURSOR_HIGH, (uint8_t)(cell >> 8));
}
