#include <facetos/libc.h>

int facet_auxv_validate(size_t count, const FacetAuxvEntry entries[])
{
    if (count != 0 && entries == NULL) return -1;
    for (size_t i = 0; i < count; i++) {
        /* Standard ELF and seL4 keys are below AT_LOOS. FacetOS owns only the
         * OS interval and does not permit duplicate authority descriptors. */
        if (entries[i].type < AT_LOOS || entries[i].type > AT_HIOS)
            return -1;
        for (size_t j = 0; j < i; j++)
            if (entries[j].type == entries[i].type)
                return -1;
    }
    return 0;
}
