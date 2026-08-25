#pragma once

#include <stdint.h>

typedef int (*SeatPCPortRead)(void *context, uint16_t port, uint8_t *value);
typedef int (*SeatPCPortWrite)(void *context, uint16_t port, uint8_t value);

typedef struct SeatPCCursorIO {
    SeatPCPortRead read;
    SeatPCPortWrite write;
    void *context;
} SeatPCCursorIO;

int seat_pc_cursor_enable(const SeatPCCursorIO *io);
int seat_pc_cursor_set(const SeatPCCursorIO *io, uint16_t cell);
