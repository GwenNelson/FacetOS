#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <facetos/libc.h>

static _Thread_local int facet_errno;
static char *const *facet_environment;
static const FacetAuxvEntry *facet_auxiliary_vector;

void facet_libc_initialize(char *const environment[],
                           const FacetAuxvEntry auxiliary_vector[])
{
    facet_environment = environment;
    facet_auxiliary_vector = auxiliary_vector;
}

char *const *facet_libc_environment(void)
{
    return facet_environment;
}

const FacetAuxvEntry *facet_libc_auxiliary_vector(void)
{
    return facet_auxiliary_vector;
}

char *getenv(const char *name)
{
    if (name[0] == '\0' || strchr(name, '=') != NULL)
        return NULL;
    size_t length = strlen(name);
    char *const *environment = facet_libc_environment();
    if (environment == NULL) return NULL;
    for (; *environment != NULL; environment++)
        if (strncmp(*environment, name, length) == 0 &&
            (*environment)[length] == '=')
            return (char *)(uintptr_t)(*environment + length + 1);
    return NULL;
}

int *__errno_location(void)
{
    return &facet_errno;
}

__attribute__((noreturn)) void __assert_fail(const char *condition,
                                             const char *file,
                                             unsigned int line,
                                             const char *function)
{
    (void)condition;
    (void)file;
    (void)line;
    (void)function;
    __builtin_trap();
    for (;;) { }
}
