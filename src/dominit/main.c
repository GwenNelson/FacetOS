#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IDebug.h>
#include <facetos/interfaces/IGenericObject.h>

#include <stdint.h>

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
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IDebug_MetaData) != FACET_OK)
        return 1;

    IGenericObject *root;
    if (platform_init(&argc, &argv, &root) != FACET_OK)
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
        (void)platform_yield();
}
