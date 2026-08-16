#include <sel4/sel4.h>

// temporary - we'll build a klibc or something later
char *strcpy(char *dest, const char *src) {
    char *ret = dest;

    while ((*dest++ = *src++) != '\0')
        ;

    return ret;
}

int main() {
    seL4_DebugPutString("FacetOS says hello!\n");

    for (;;) {
        seL4_Yield();
    }
}
