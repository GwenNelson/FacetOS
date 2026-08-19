#pragma once

void platform_sel4_bootinfo_dump(const seL4_BootInfo *bi);
seL4_BootInfo* platform_sel4_get_bootinfo(void);
