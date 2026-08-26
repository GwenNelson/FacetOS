#include <stddef.h>

static _Thread_local int facet_errno;

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
