#pragma once

#include <facetos/interfaces/IPageAllocator.h>

/* The static heap is active from process entry.  Once the bootstrap page
 * allocator proxy is available, all new allocations switch to liballoc. */
int dominit_allocator_use_pages(IPageAllocator *allocator);

