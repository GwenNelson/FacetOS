#include <facetos/dominit/platform/api.h>
#include <facetos/libfacet/platform/sel4/client.h>

#include <stdint.h>

#define SEL4_BOOTSTRAP_ARGUMENT_COUNT 4

static int
parse_word(const char *text, uint64_t *result)
{
    if (text == NULL || result == NULL || *text == '\0')
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

static void
consume_bootstrap_arguments(int *argc, char **argv)
{
    int original_argc = *argc;
    for (int destination = 1;
         destination + SEL4_BOOTSTRAP_ARGUMENT_COUNT < original_argc;
         destination++) {
        argv[destination] =
            argv[destination + SEL4_BOOTSTRAP_ARGUMENT_COUNT];
    }

    *argc = original_argc - SEL4_BOOTSTRAP_ARGUMENT_COUNT;
    argv[*argc] = NULL;
}

FacetResult
platform_init(int *argc, char ***argv, IGenericObject **out_root)
{
    if (out_root == NULL)
        return FACET_INVALID_ARGUMENT;
    *out_root = NULL;

    if (argc == NULL || argv == NULL || *argv == NULL ||
        *argc < SEL4_BOOTSTRAP_ARGUMENT_COUNT + 1) {
        return FACET_INVALID_ARGUMENT;
    }

    char **arguments = *argv;
    for (int i = 0; i <= SEL4_BOOTSTRAP_ARGUMENT_COUNT; i++) {
        if (arguments[i] == NULL)
            return FACET_INVALID_ARGUMENT;
    }

    uint64_t endpoint;
    uint64_t receive_cnode;
    uint64_t receive_slot;
    uint64_t receive_depth;
    if (parse_word(arguments[1], &endpoint) != 0 || endpoint == 0 ||
        parse_word(arguments[2], &receive_cnode) != 0 || receive_cnode == 0 ||
        parse_word(arguments[3], &receive_slot) != 0 || receive_slot == 0 ||
        parse_word(arguments[4], &receive_depth) != 0 || receive_depth == 0) {
        return FACET_INVALID_ARGUMENT;
    }

    FacetResult result = facet_sel4_client_init(
        receive_cnode, receive_slot, receive_depth);
    if (result != FACET_OK)
        return result;

    IGenericObject *root = libfacet_proxy_from(endpoint);
    if (root == NULL)
        return FACET_ERROR;

    consume_bootstrap_arguments(argc, arguments);
    *out_root = root;
    return FACET_OK;
}

FacetResult
platform_yield(void)
{
    return facet_sel4_client_yield();
}
