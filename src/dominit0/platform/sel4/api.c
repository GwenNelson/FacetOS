#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/interfaces/IFrameAllocator.h>

#include <sel4/sel4.h>
#include <sel4runtime.h>

#include <simple-default/simple-default.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <vka/vka.h>

#include <stddef.h>

#include "bootinfo.h"

extern seL4_BootInfo* platform_sel4_bi;

#define ALLOCMAN_BOOTSTRAP_POOL_SIZE (64 * 1024)

static simple_t sel4_simple;
static allocman_t *sel4_allocman;
static vka_t sel4_vka;

static IFrameAllocator *frame_allocator;
static IFrameAllocator frame_alloc_instance;

size_t sel4_frame_alloc_get_frame_size(void* self) {
	// temporary hack
	return 4096;
}

int sel4_frame_alloc_alloc(void* self, size_t count, IFrame **frames) {
	return -1;
}

int sel4_frame_alloc_free(void* self, size_t count, IFrame **frames) {
	return -1;
}

IFrameAllocator* sel4_frame_allocator_create(vka_t *vka) {
	klog(LOG_WARN,"sel4_frame_allocator_create() - not yet fully implemented!\n");
	IFrameAllocator *retval = &frame_alloc_instance;
	retval->self = (void*)retval;
	retval->priv = (void*)vka;
	retval->get_frame_size = &sel4_frame_alloc_get_frame_size;
	retval->alloc          = &sel4_frame_alloc_alloc;
	retval->free           = &sel4_frame_alloc_free;
	return NULL; // cos it's not yet implemented yet
}

void platform_init_early(void) {
}

void platform_init(void) {
     klog(LOG_INFO,"platform_init() for seL4\n");
     platform_sel4_bi = platform_sel4_get_bootinfo();
     platform_sel4_bootinfo_dump(platform_sel4_bi);

     klog(LOG_DEBUG,"platform_init() - allocating %llu for allocman_pool\n", ALLOCMAN_BOOTSTRAP_POOL_SIZE);
     void* allocman_pool;
     simple_default_init_bootinfo(&sel4_simple, platform_sel4_bi);
     allocman_pool = kmalloc(ALLOCMAN_BOOTSTRAP_POOL_SIZE);

     if(allocman_pool==NULL) kpanic("Unable to allocate allocman bootstrap pool!");

     klog(LOG_DEBUG,"platform_init() - setting up allocman\n");
     sel4_allocman = bootstrap_use_current_simple(
		&sel4_simple,
		ALLOCMAN_BOOTSTRAP_POOL_SIZE,
		allocman_pool);

     if (sel4_allocman == NULL) kpanic("Unable to bootstrap seL4 allocman!");

     klog(LOG_DEBUG,"platform_init() - setting up VKA\n");
     allocman_make_vka(&sel4_vka, sel4_allocman);

     klog(LOG_DEBUG,"platform_init() - setting up IFrameAllocator\n");
     frame_allocator = sel4_frame_allocator_create(&sel4_vka);
     if(frame_allocator == NULL) kpanic("Unable to create frame allocator instance!");

     klog(LOG_INFO,"seL4 platform ready\n");
}

void platform_yield(void) {
     seL4_Yield();
}
