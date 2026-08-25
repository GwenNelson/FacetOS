#pragma once

#include <facetos/interfaces/IPageAllocator.h>

/* Each application links its own allocator state.  The bootstrap heap is
 * private to that process.  Page-backed applications may hand off to their
 * delegated page service; fixed services may instead make the unused static
 * region reclaimable without acquiring any further authority. */
int facet_app_allocator_use_pages(IPageAllocator *allocator);
int facet_app_allocator_use_static(void);
