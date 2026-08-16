#include <sel4/sel4.h>

int main() {
    seL4_DebugPutString("FacetOS says hello!\n");

    for (;;) {
        seL4_Yield();
    }
}
