#include <facetos/dominit0/platform/api.h>
#include <sel4/sel4.h>
#include <sel4runtime.h>

#include "bootinfo.h"

extern seL4_BootInfo* platform_sel4_bi;

void platform_init_early(void) {
}

void platform_init(void) {
     platform_sel4_bi = platform_sel4_get_bootinfo();
     platform_sel4_bootinfo_dump(platform_sel4_bi);
}

void platform_yield(void) {
     seL4_Yield();
}
