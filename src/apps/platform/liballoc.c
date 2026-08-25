/*
 * Applications use the same audited allocator algorithm as dominit0, but the
 * hooks and all allocator state are linked separately into each executable.
 */
#ifdef DEBUG
#undef DEBUG
#endif
#include "../../dominit0/liballoc.c"
