#pragma once

#include <stddef.h>

#include <facetos/interfaces/IPageAllocator.h>

// Fucking 90MB bootstrap, 80MB of this will go to allocman, which should be enough for most realistic systems
#define KMALLOC_BOOTSTRAP_HEAP_SIZE (90UL * 1024 * 1024)
#define KMALLOC_ALIGNMENT sizeof(uintptr_t)

// these two functions should be obvious
void *kmalloc(size_t size);
void  kfree(void* p);

// this function is called at startup before anything else
void kmalloc_init_early();

// this function is called only once there's page allocation in place
void kmalloc_init(IPageAllocator *allocator);

void kmalloc_dump(void); // used to spit out debugging stuff
