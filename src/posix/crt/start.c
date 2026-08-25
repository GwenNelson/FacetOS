#include <facet_posix_runtime.h>

#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <facetos/libfacet/platform/sel4/client.h>

#include <sel4/sel4.h>
#include <sel4runtime/start.h>

#include <stdint.h>

static IPOSIXView *posix_view;

IPOSIXView *facet_posix_view(void)
{
    return posix_view;
}

static int parse_word(const char *text, uint64_t *out)
{
    if (text == NULL || out == NULL || *text == '\0') return -1;
    uint64_t value = 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9' ||
            value > (UINT64_MAX - (uint64_t)(*text - '0')) / 10)
            return -1;
        value = value * 10 + (uint64_t)(*text++ - '0');
    }
    *out = value;
    return 0;
}

static void consume_bootstrap(int *argc, char **argv)
{
    for (int i = 1; i + 4 < *argc; i++) argv[i] = argv[i + 4];
    *argc -= 4;
    argv[*argc] = NULL;
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

    uint64_t endpoint, cnode, receive_slot, depth;
    if (argc_word < 5 || parse_word(argv[1], &endpoint) != 0 ||
        parse_word(argv[2], &cnode) != 0 ||
        parse_word(argv[3], &receive_slot) != 0 ||
        parse_word(argv[4], &depth) != 0 ||
        libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IPOSIXView_MetaData) != FACET_OK ||
        facet_sel4_client_init(cnode, receive_slot, depth) != FACET_OK)
        goto stopped;
    IGenericObject *root = libfacet_proxy_from(endpoint);
    if (root == NULL) goto stopped;
    posix_view = libfacet_proxy_client_get_interface(root, IID_IPOSIXView);
    if (posix_view == NULL) goto stopped;
    int argc = (int)argc_word;
    consume_bootstrap(&argc, argv);
    (void)main(argc, (const char *const *)argv,
               (const char *const *)envp);
stopped:
    for (;;) seL4_Yield();
}
