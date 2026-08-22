#include <facetos/interfaces/IDebug.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/libfacet/platform/sel4/client.h>

#include <stdint.h>

static int
parse_word(const char *text, uint64_t *result)
{
    if (text == 0 || result == 0 || *text == '\0')
        return -1;

    uint64_t base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
        if (*text == '\0')
            return -1;
    }

    uint64_t value = 0;
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

        if (digit >= base || value > (UINT64_MAX - digit) / base)
            return -1;
        value = value * base + digit;
        text++;
    }

    *result = value;
    return 0;
}

static void debug_putc(IDebug *debug, char c)
{
    (void)debug->putc(debug->self, (uint8_t)(unsigned char)c);
}

static void debug_puts(IDebug *debug, const char *s)
{
    while (*s)
        debug_putc(debug, *s++);
}

int main(int argc, char **argv)
{
    if (argc < 5)
        return 1;

    uint64_t endpoint;
    uint64_t receive_cnode;
    uint64_t receive_slot;
    uint64_t receive_depth;
    if (parse_word(argv[1], &endpoint) != 0 || endpoint == 0 ||
        parse_word(argv[2], &receive_cnode) != 0 || receive_cnode == 0 ||
        parse_word(argv[3], &receive_slot) != 0 || receive_slot == 0 ||
        parse_word(argv[4], &receive_depth) != 0 || receive_depth == 0)
        return 1;

    if (facet_sel4_client_init(receive_cnode, receive_slot,
                               receive_depth) != FACET_OK)
        return 1;

    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDebug_MetaData) != FACET_OK)
        return 1;

    IGenericObject *root = libfacet_proxy_from(endpoint);
    if (root == NULL)
        return 1;

    IDebug *debug = (IDebug *)libfacet_proxy_client_get_interface(
        root, IID_IDebug);
    if (debug == NULL) {
        libfacet_free_proxy_client(root);
        return 1;
    }

    debug_puts(debug, "OH FUCK I AM A SEPARATE PROCESS\n");

    libfacet_free_proxy_client(debug);
    libfacet_free_proxy_client(root);

    for (;;)
        (void)facet_sel4_client_yield();
}
