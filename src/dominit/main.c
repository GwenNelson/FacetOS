#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IDebug.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPageAllocator.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "platform/allocator.h"

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
        libfacet_register_interface_metadata(&IDebug_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPageAllocator_MetaData) != FACET_OK)
        return 1;

    IGenericObject *root;
    IPageAllocator *page_allocator;
    if (platform_init(&argc, &argv, &root, &page_allocator) != FACET_OK)
        return 1;

    IDebug *debug = (IDebug *)libfacet_proxy_client_get_interface(
        root, IID_IDebug);
    if (debug == NULL) {
        libfacet_free_proxy_client(page_allocator);
        libfacet_free_proxy_client(root);
        return 1;
    }

    debug_puts(debug, "child received IPageAllocator\n");
    if (dominit_allocator_use_pages(page_allocator) != 0) {
        debug_puts(debug, "child could not activate IPageAllocator\n");
        libfacet_free_proxy_client(debug);
        libfacet_free_proxy_client(page_allocator);
        libfacet_free_proxy_client(root);
        return 1;
    }
    debug_puts(debug, "child activated IPageAllocator\n");

    /* This is deliberately larger than the bootstrap heap's remaining
     * liballoc region. It proves allocations now reach this domain's
     * IPageAllocator rather than dominit0's own allocator. */
    void *allocation_probe = malloc(512u * 1024u);
    if (allocation_probe == NULL) {
        libfacet_free_proxy_client(debug);
        libfacet_free_proxy_client(root);
        return 1;
    }
    free(allocation_probe);

    debug_puts(debug, "child IPageAllocator/liballoc ready\n");

    libfacet_free_proxy_client(debug);
    libfacet_free_proxy_client(root);

    /* allocator.c retains this proxy for every later liballoc page request. */

    for (;;)
        (void)platform_yield();
}
