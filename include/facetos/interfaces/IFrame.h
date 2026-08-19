#pragma once

#include <stddef.h>
#include <facetos/uuid.h>

static const uuid_t IID_IFrame = UUID_INIT(0x1d35ab9a,0xe774,0x45b5,0x896a,0x6f0080e4bc21ULL);

// this is NOT final
typedef struct IFrame {
	void *self; // required by ALL interfaces
	size_t (*get_frame_size)(void* self);
} IFrame;
