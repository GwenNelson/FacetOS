/*
 * dominit uses the same liballoc implementation as dominit0.  The allocation
 * hooks live in allocator.c and call this child's IPageAllocator proxy.
 * Keep DEBUG disabled here: dominit0's optional diagnostics depend on klog.
 */
#ifdef DEBUG
#undef DEBUG
#endif
#include "../../dominit0/liballoc.c"
