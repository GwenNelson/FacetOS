#include "api.h"

#include <sel4/sel4.h>

#include <stdbool.h>
#include <stdint.h>

static seL4_CPtr device0_cap;
static seL4_CPtr device1_cap;
static volatile uint16_t *vga_memory;

#if defined(FACET_SEAT_PC)
static bool left_shift;
static bool right_shift;
static bool alt;
static bool extended;

static int crtc_write(uint8_t index, uint8_t value)
{
    if (seL4_X86_IOPort_Out8(device1_cap, 0x3d4, index) != seL4_NoError)
        return -1;
    return seL4_X86_IOPort_Out8(device1_cap, 0x3d5, value) == seL4_NoError
        ? 0 : -1;
}

static int update_cursor(size_t cursor)
{
    if (cursor > UINT16_MAX) return -1;
    if (crtc_write(0x0f, (uint8_t)cursor) != 0 ||
        crtc_write(0x0e, (uint8_t)(cursor >> 8)) != 0)
        return -1;
    return 0;
}

static uint8_t translate_key(uint8_t code, bool shift)
{
    static const uint8_t plain[58] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',
        [10]='9',[11]='0',[12]='-',[13]='=',[14]='\b',[15]='\t',
        [16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',
        [23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\r',
        [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',
        [37]='k',[38]='l',[39]=';',[40]='\'',[41]='`',[43]='\\',
        [44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',
        [51]=',',[52]='.',[53]='/',[57]=' ',
    };
    static const uint8_t shifted[58] = {
        [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',
        [10]='(',[11]=')',[12]='_',[13]='+',[14]='\b',[15]='\t',
        [16]='Q',[17]='W',[18]='E',[19]='R',[20]='T',[21]='Y',[22]='U',
        [23]='I',[24]='O',[25]='P',[26]='{',[27]='}',[28]='\r',
        [30]='A',[31]='S',[32]='D',[33]='F',[34]='G',[35]='H',[36]='J',
        [37]='K',[38]='L',[39]=':',[40]='"',[41]='~',[43]='|',
        [44]='Z',[45]='X',[46]='C',[47]='V',[48]='B',[49]='N',[50]='M',
        [51]='<',[52]='>',[53]='?',[57]=' ',
    };
    return code < sizeof(plain) ? (shift ? shifted[code] : plain[code]) : 0;
}
#endif

int seat_platform_initialize(uint64_t device0, uint64_t device1,
                             uint64_t vga_address)
{
    if (device0 == 0 || device0 > UINT32_MAX || device1 > UINT32_MAX)
        return -1;
    device0_cap = (seL4_CPtr)device0;
    device1_cap = (seL4_CPtr)device1;
#if defined(FACET_SEAT_SERIAL)
    (void)vga_address;
    (void)seL4_X86_IOPort_Out8(device0_cap, 0x3f9, 0x00);
    (void)seL4_X86_IOPort_Out8(device0_cap, 0x3fb, 0x80);
    (void)seL4_X86_IOPort_Out8(device0_cap, 0x3f8, 0x01);
    (void)seL4_X86_IOPort_Out8(device0_cap, 0x3f9, 0x00);
    (void)seL4_X86_IOPort_Out8(device0_cap, 0x3fb, 0x03);
    (void)seL4_X86_IOPort_Out8(device0_cap, 0x3fa, 0xc7);
    (void)seL4_X86_IOPort_Out8(device0_cap, 0x3fc, 0x0b);
    return 0;
#else
    if (device1_cap == seL4_CapNull || vga_address == 0) return -1;
    vga_memory = (volatile uint16_t *)(uintptr_t)vga_address;
    /* Standard colour-text cursor shape, explicitly enabled. */
    if (crtc_write(0x0a, 0x0d) != 0 || crtc_write(0x0b, 0x0f) != 0)
        return -1;
    return update_cursor(0);
#endif
}

int seat_platform_read_byte(uint8_t *byte)
{
#if defined(FACET_SEAT_SERIAL)
    if (byte == NULL) return -1;
    seL4_X86_IOPort_In8_t status = seL4_X86_IOPort_In8(device0_cap, 0x3fd);
    if (status.error != seL4_NoError || (status.result & 1u) == 0) return 1;
    seL4_X86_IOPort_In8_t value = seL4_X86_IOPort_In8(device0_cap, 0x3f8);
    if (value.error != seL4_NoError) return -1;
    *byte = value.result;
    return 0;
#else
    (void)byte;
    return -1;
#endif
}

int seat_platform_write(const uint8_t *data, size_t size)
{
#if defined(FACET_SEAT_SERIAL)
    if (data == NULL && size != 0) return -1;
    for (size_t i = 0; i < size; i++) {
        unsigned spins = 1000000;
        while (spins-- != 0) {
            seL4_X86_IOPort_In8_t status =
                seL4_X86_IOPort_In8(device0_cap, 0x3fd);
            if (status.error != seL4_NoError) return -1;
            if ((status.result & 0x20u) != 0) break;
        }
        if (spins == 0 || seL4_X86_IOPort_Out8(
                device0_cap, 0x3f8, data[i]) != seL4_NoError)
            return -1;
    }
    return 0;
#else
    (void)data; (void)size;
    return -1;
#endif
}

int seat_platform_present(const uint16_t *cells, size_t count, size_t cursor)
{
#if defined(FACET_SEAT_PC)
    if (cells == NULL || count > 80u * 25u || vga_memory == NULL) return -1;
    for (size_t i = 0; i < count; i++) vga_memory[i] = cells[i];
    return update_cursor(cursor);
#else
    (void)cells; (void)count; (void)cursor;
    return -1;
#endif
}

int seat_platform_poll_key(SeatKey *key)
{
#if defined(FACET_SEAT_PC)
    if (key == NULL) return -1;
    *key = (SeatKey){0};
    seL4_X86_IOPort_In8_t status = seL4_X86_IOPort_In8(device0_cap, 0x64);
    if (status.error != seL4_NoError) return -1;
    if ((status.result & 1u) == 0) return 1;
    seL4_X86_IOPort_In8_t input = seL4_X86_IOPort_In8(device0_cap, 0x60);
    if (input.error != seL4_NoError) return -1;
    uint8_t code = input.result;
    if (code == 0xe0) { extended = true; return 1; }
    bool released = (code & 0x80u) != 0;
    code &= 0x7fu;
    if (code == 42) left_shift = !released;
    else if (code == 54) right_shift = !released;
    else if (code == 56) alt = !released;
    if (released || extended) { extended = false; return 1; }
    if (alt && code >= 59 && code <= 63) {
        key->kind = SEAT_KEY_SWITCH_TERMINAL;
        key->terminal_index = (uint8_t)(code - 59);
        return 0;
    }
    key->byte = translate_key(code, left_shift || right_shift);
    if (key->byte == 0) return 1;
    key->kind = SEAT_KEY_BYTE;
    return 0;
#else
    (void)key;
    return -1;
#endif
}
