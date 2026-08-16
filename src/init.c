#include <sel4/sel4.h>

// temporary - we'll build a klibc or something later

/*char *strcpy(char *dest, const char *src)
{
    if (!src) {
        __asm__ volatile("ud2");
    }

    if (!dest) {
        __asm__ volatile("int3");
    }

    char *ret = dest;

    while ((*dest++ = *src++))
        ;

    return ret;
}*/

char *strcpy(char *dest, const char *src) {
    char *ret = dest;

    while ((*dest++ = *src++) != '\0')
        ;

    return ret;
}

void main(int argc, char **argv, char **envp) {
    seL4_DebugPutString("FacetOS says hello!\n");

    for (;;) {
        seL4_Yield();
    }
}
