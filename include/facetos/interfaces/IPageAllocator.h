#pragma once

#include <stddef.h>

#include <facetos/uuid.h>

static const uuid_t IID_IPageAllocator = UUID_INIT(0xbb0edfa1,0xfb32,0x476f,0xa37a,0x12380b12757cULL);

typedef struct IPageAllocator {
	void *self; // required by ALL interfaces
	void *priv; // private data
	size_t (*get_page_size)(void* self);
	int    (*alloc)         (void *self, size_t count, void **pages);  // returns number of pages, -1 on error
	int    (*free)          (void *self, size_t count, void *base);    // returns number of pages freed, -1 on error
} IPageAllocator;
