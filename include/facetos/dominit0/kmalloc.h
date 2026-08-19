#pragma once

#include <stddef.h>

#include <facetos/interfaces/IFrameAllocator.h>

// 8MB bootstrap
#define KMALLOC_BOOTSTRAP_HEAP_SIZE (8UL * 1024 * 1024)
#define KMALLOC_ALIGNMENT sizeof(uintptr_t)

// these two functions should be obvious
void *kmalloc(size_t size);
void  kfree(void* p);

// this function is called at startup before anything else
void kmalloc_init_early();

// this function is called only once there's page allocation in place
void kmalloc_init(IFrameAllocator *allocator);
