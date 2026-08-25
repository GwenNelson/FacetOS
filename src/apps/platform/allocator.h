#pragma once

#include <facetos/interfaces/IPageAllocator.h>

/* Each application links its own allocator state.  The bootstrap heap is
 * private to that process and hands off only to its delegated page service. */
int facet_app_allocator_use_pages(IPageAllocator *allocator);
