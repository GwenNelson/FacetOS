#pragma once

#include <stddef.h>

#include <facetos/interfaces/IFrameAllocator.h>

// Fucking 128MB bootstrap, 80MB of this will go to allocman, which should be enough for most realistic systems
#define KMALLOC_BOOTSTRAP_HEAP_SIZE (128UL * 1024 * 1024)
#define KMALLOC_ALIGNMENT sizeof(uintptr_t)

// these two functions should be obvious
void *kmalloc(size_t size);
void  kfree(void* p);

// this function is called at startup before anything else
void kmalloc_init_early();

// this function is called only once there's page allocation in place
void kmalloc_init(IFrameAllocator *allocator);
