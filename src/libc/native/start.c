#include <sel4/sel4.h>
#define main sel4runtime_declared_main
#include <sel4runtime/start.h>
#undef main

#include <stddef.h>
#include <stdlib.h>

#include <facetos/libc.h>

extern int main(int argc, char **argv);

__attribute__((noreturn)) void facet_native_start(const void *stack)
{
    unsigned long argc = *(const unsigned long *)stack;
    char **argv = (char **)((const unsigned long *)stack + 1);
    char **envp = &argv[argc + 1];
    size_t envc = 0;
    while (envp[envc] != NULL) envc++;
    const auxv_t *auxv = (const auxv_t *)&envp[envc + 1];
    __sel4runtime_load_env((int)argc, (const char *const *)argv,
                           (const char *const *)envp, auxv);
    facet_libc_initialize(envp, (const FacetAuxvEntry *)auxv);
    exit(main((int)argc, argv));
}
