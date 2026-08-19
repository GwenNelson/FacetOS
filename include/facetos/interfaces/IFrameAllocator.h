#pragma once

#include <stddef.h>

#include <facetos/uuid.h>
#include <facetos/interfaces/IFrame.h>

static const uuid_t IID_IFrameAllocator = UUID_INIT(0xbb0edfa1,0xfb32,0x476f,0xa37a,0x12380b12757cULL);

typedef struct IFrameAllocator {
	void *self; // required by ALL interfaces
	void *priv; // private data
	size_t (*get_frame_size)(void* self);
	int    (*alloc)         (void *self, size_t count, IFrame **frames);
	int    (*free)          (void *self, size_t count, IFrame **frame);
} IFrameAllocator;
