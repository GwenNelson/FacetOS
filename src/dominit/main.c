#include <sel4/sel4.h>

static seL4_CPtr debug_ep;

static int
parse_word(const char *text, seL4_Word *result)
{
    if (text == 0 || result == 0 || *text == '\0')
        return -1;

    seL4_Word base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
        if (*text == '\0')
            return -1;
    }

    seL4_Word value = 0;
    while (*text != '\0') {
        unsigned int digit;
        if (*text >= '0' && *text <= '9') {
            digit = (unsigned int)(*text - '0');
        } else if (*text >= 'a' && *text <= 'f') {
            digit = (unsigned int)(*text - 'a') + 10;
        } else if (*text >= 'A' && *text <= 'F') {
            digit = (unsigned int)(*text - 'A') + 10;
        } else {
            return -1;
        }

        if (digit >= base ||
            value > (~(seL4_Word)0 - digit) / base) {
            return -1;
        }

        value = value * base + digit;
        text++;
    }

    *result = value;
    return 0;
}

static void debug_putc(char c)
{
    seL4_SetMR(0, (seL4_Word)(unsigned char)c);

    seL4_MessageInfo_t info =
        seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_Send(debug_ep, info);
}

static void debug_puts(const char *s)
{
    while (*s)
        debug_putc(*s++);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 1;

    seL4_Word endpoint;
    if (parse_word(argv[1], &endpoint) != 0 || endpoint == seL4_CapNull)
        return 1;

    debug_ep = (seL4_CPtr)endpoint;

    debug_puts("OH FUCK I AM A SEPARATE PROCESS\n");

    for (;;)
        seL4_Yield();
}
