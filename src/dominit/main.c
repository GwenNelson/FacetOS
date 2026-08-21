#include <sel4/sel4.h>
#include <stdlib.h>

static seL4_CPtr debug_ep;

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

    debug_ep = (seL4_CPtr)strtoul(argv[1], NULL, 0);

    debug_puts("OH FUCK I AM A SEPARATE PROCESS\n");

    for (;;)
        seL4_Yield();
}
