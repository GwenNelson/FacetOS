#include <facetos/libc.h>

#include <errno.h>
#include <stddef.h>

unsigned long getauxval(unsigned long type)
{
    const FacetAuxvEntry *entry = facet_libc_auxiliary_vector();
    if (entry != NULL) {
        for (; entry->type != 0; entry++)
            if ((unsigned long)entry->type == type)
                return (unsigned long)entry->value;
    }
    errno = ENOENT;
    return 0;
}
