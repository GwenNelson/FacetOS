#include <facet_posix_runtime.h>

#include <facetos/libc.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/libfacet/platform/sel4/client.h>

#include <sel4/sel4.h>
#define main sel4runtime_declared_main
#include <sel4runtime/start.h>
#undef main

#include <stdint.h>
#include <stdlib.h>

static IPOSIXView *posix_view;
static IPageAllocator page_allocator;

IPOSIXView *facet_posix_view(void)
{
    return posix_view;
}

extern int facet_app_allocator_use_pages(IPageAllocator *allocator);
extern int main(int argc, char **argv);

static FacetResult page_size(void *self, uint64_t *size)
{
    (void)self;
    if (size == NULL) return FACET_INVALID_ARGUMENT;
    *size = 4096;
    return FACET_OK;
}

static FacetResult allocate_pages(void *self, uint64_t count, void **pages)
{
    (void)self;
    return posix_view->allocate_pages(posix_view->self, count, pages);
}

static FacetResult free_pages(void *self, uint64_t count, uint64_t base)
{
    (void)self;
    return posix_view->free_pages(posix_view->self, count, base);
}

static int lookup_auxv(const auxv_t *auxv, uintptr_t type, uint64_t *out)
{
    if (auxv == NULL || out == NULL) return -1;
    for (const auxv_t *entry = auxv; entry->a_type != AT_NULL; entry++) {
        if ((uintptr_t)entry->a_type == type) {
            *out = (uint64_t)(uintptr_t)entry->a_un.a_val;
            return 0;
        }
    }
    return -1;
}

__attribute__((noreturn)) void facet_posix_start(const void *stack)
{
    unsigned long argc_word = *(const unsigned long *)stack;
    char **argv = (char **)((const unsigned long *)stack + 1);
    char **envp = &argv[argc_word + 1];
    size_t envc = 0;
    while (envp[envc] != NULL) envc++;
    const auxv_t *auxv = (const auxv_t *)&envp[envc + 1];
    __sel4runtime_load_env((int)argc_word, (const char *const *)argv,
                           (const char *const *)envp, auxv);
    facet_libc_initialize(envp, (const FacetAuxvEntry *)auxv);

    uint64_t abi_version, endpoint, cnode, receive_slot, depth;
    if (lookup_auxv(auxv, AT_FACET_ABI_VERSION, &abi_version) != 0 ||
        abi_version != FACETOS_STARTUP_ABI_VERSION ||
        lookup_auxv(auxv, AT_FACET_ROOT_OBJECT, &endpoint) != 0 ||
        lookup_auxv(auxv, AT_FACET_RECEIVE_CNODE, &cnode) != 0 ||
        lookup_auxv(auxv, AT_FACET_RECEIVE_SLOT, &receive_slot) != 0 ||
        lookup_auxv(auxv, AT_FACET_RECEIVE_DEPTH, &depth) != 0 ||
        libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPOSIXView_MetaData) != FACET_OK ||
        facet_sel4_client_init(cnode, receive_slot, depth) != FACET_OK)
        goto stopped;
    IGenericObject *root = libfacet_proxy_from(endpoint);
    if (root == NULL) goto stopped;
    posix_view = libfacet_proxy_client_get_interface(root, IID_IPOSIXView);
    if (posix_view == NULL) goto stopped;
    page_allocator = (IPageAllocator){
        .self = &page_allocator, .priv = &page_allocator,
        .get_page_size = page_size, .alloc = allocate_pages,
        .free = free_pages,
    };
    if (facet_app_allocator_use_pages(&page_allocator) != 0) goto stopped;
    exit(main((int)argc_word, argv));
stopped:
    for (;;) seL4_Yield();
}

void facet_posix_yield(void)
{
    seL4_Yield();
}
