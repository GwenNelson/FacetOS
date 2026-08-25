#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool facet_utf8_is_valid(const char *data, size_t size)
{
    if (data == NULL && size != 0) return false;
    size_t i = 0;
    while (i < size) {
        uint8_t first = (uint8_t)data[i++];
        if (first < 0x80) continue;
        unsigned needed;
        uint32_t value;
        if (first >= 0xc2 && first <= 0xdf) {
            needed = 1;
            value = first & 0x1fu;
        } else if (first >= 0xe0 && first <= 0xef) {
            needed = 2;
            value = first & 0x0fu;
        } else if (first >= 0xf0 && first <= 0xf4) {
            needed = 3;
            value = first & 0x07u;
        } else {
            return false;
        }
        if (needed > size - i) return false;
        for (unsigned j = 0; j < needed; j++) {
            uint8_t next = (uint8_t)data[i++];
            if ((next & 0xc0u) != 0x80u) return false;
            value = (value << 6) | (next & 0x3fu);
        }
        if ((needed == 2 && value < 0x800) ||
            (needed == 3 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}
