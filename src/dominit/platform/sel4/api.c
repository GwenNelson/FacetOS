#include <facetos/dominit/platform/api.h>
#include <facetos/libc.h>
#include <facetos/libc/native.h>
#include <facetos/interfaces/IProcessEnvironment.h>
#include <facetos/libfacet/platform/sel4/client.h>

#include <sel4runtime.h>

#include <stdint.h>

static IGenericObject *native_root;
static IProcessEnvironment *native_environment;

static int
lookup_auxv(uintptr_t type, uintptr_t *value)
{
    if (value == NULL)
        return -1;
    const auxv_t *entry = sel4runtime_auxv();
    if (entry == NULL)
        return -1;
    for (; entry->a_type != AT_NULL; entry++) {
        if ((uintptr_t)entry->a_type == type) {
            *value = (uintptr_t)entry->a_un.a_val;
            return 0;
        }
    }
    return -1;
}

FacetResult
platform_init(int *argc, char ***argv, IGenericObject **out_root)
{
    if (out_root == NULL || argc == NULL || argv == NULL || *argv == NULL)
        return FACET_INVALID_ARGUMENT;
    *out_root = NULL;

    uintptr_t abi_version = 0;
    uintptr_t endpoint = 0;
    uintptr_t receive_cnode = 0;
    uintptr_t receive_slot = 0;
    uintptr_t receive_depth = 0;
    if (lookup_auxv(AT_FACET_ABI_VERSION, &abi_version) != 0 ||
        abi_version != FACETOS_STARTUP_ABI_VERSION ||
        lookup_auxv(AT_FACET_ROOT_OBJECT, &endpoint) != 0 || endpoint == 0 ||
        lookup_auxv(AT_FACET_RECEIVE_CNODE, &receive_cnode) != 0 ||
        receive_cnode == 0 ||
        lookup_auxv(AT_FACET_RECEIVE_SLOT, &receive_slot) != 0 ||
        receive_slot == 0 ||
        lookup_auxv(AT_FACET_RECEIVE_DEPTH, &receive_depth) != 0 ||
        receive_depth == 0) {
        return FACET_INVALID_ARGUMENT;
    }

    FacetResult result = facet_sel4_client_init(
        receive_cnode, receive_slot, receive_depth);
    if (result != FACET_OK)
        return result;

    IGenericObject *root = libfacet_proxy_from(endpoint);
    if (root == NULL)
        return FACET_ERROR;

    *out_root = root;
    native_root = root;
    return FACET_OK;
}

IGenericObject *facet_native_root(void)
{
    return native_root;
}

IProcessEnvironment *facet_native_environment(void)
{
    if (native_environment == NULL && native_root != NULL)
        native_environment = libfacet_proxy_client_get_interface(
            native_root, IID_IProcessEnvironment);
    return native_environment;
}

FacetResult
platform_yield(void)
{
    return facet_sel4_client_yield();
}
