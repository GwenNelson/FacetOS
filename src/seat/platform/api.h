#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum SeatKeyKind {
    SEAT_KEY_NONE = 0,
    SEAT_KEY_BYTE,
    SEAT_KEY_SWITCH_TERMINAL,
} SeatKeyKind;

typedef struct SeatKey {
    SeatKeyKind kind;
    uint8_t byte;
    uint8_t terminal_index;
} SeatKey;

int seat_platform_initialize(uint64_t device0, uint64_t device1,
                             uint64_t vga_address);
int seat_platform_read_byte(uint8_t *byte);
int seat_platform_write(const uint8_t *data, size_t size);
int seat_platform_present(const uint16_t *cells, size_t count, size_t cursor);
int seat_platform_poll_key(SeatKey *key);

