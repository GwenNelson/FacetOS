#pragma once

#include <facetos/uuid.h>

#include <stddef.h>

static const uuid_t IID_IGenericObject = UUID_INIT(0xb8713abf,0x0c5b,0x4f2d,0x87be,0x90e9494ba2b0ULL);

static const size_t IGenericObject_RequiredInterfacesCount = 0;

typedef struct IGenericObject {
	void   *self; // required by ALL interfaces
	void   *priv; // private data
	void* (*getInterface)(void* self, uuid_t iid);  // Returns the requested interface exposed by this object,
							// or NULL if the object does not support it.
	// fill in your methods and variables here, remember to always make first argument void* self
} IGenericObject;
