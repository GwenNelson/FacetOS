#include <facetos/libc.h>

#include <errno.h>
#include <stddef.h>
#include <sel4runtime.h>

unsigned long getauxval(unsigned long type)
{
    const auxv_t *entry = sel4runtime_auxv();
    if (entry != NULL) {
        for (; entry->a_type != AT_NULL; entry++)
            if ((unsigned long)entry->a_type == type)
                return (unsigned long)entry->a_un.a_val;
    }
    errno = ENOENT;
    return 0;
}
