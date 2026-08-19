#pragma once

#include <stddef.h>

#include <facetos/uuid.h>
#include <facetos/interfaces/IGenericObject.h>

static const uuid_t IID_IPageAllocator = UUID_INIT(0xbb0edfa1, 0xfb32, 0x476f, 0xa37a, 0x12380b12757cULL);

static const char IPageAllocator_InterfaceName[] = "IPageAllocator";

static const uuid_t IPageAllocator_RequiredInterfaces[] = {
    IID_IGenericObject,
};

static const size_t IPageAllocator_RequiredInterfacesCount =
    sizeof(IPageAllocator_RequiredInterfaces) /
    sizeof(IPageAllocator_RequiredInterfaces[0]);

typedef struct IPageAllocator {
    void *self; // required by ALL interfaces
    void *priv; // private data

    void *(*getInterface)(void *self, uuid_t iid);

    size_t (*get_page_size)(void *self);

    // Returns 0 on success, -1 on error.
    int (*alloc)(void *self, size_t count, void **pages);

    // Returns 0 on success, -1 on error.
    int (*free)(void *self, size_t count, void *base);

} IPageAllocator;
