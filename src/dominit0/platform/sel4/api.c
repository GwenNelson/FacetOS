#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/interfaces/IPageAllocator.h>

#include <sel4/sel4.h>
#include <sel4runtime.h>

#include <simple-default/simple-default.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <vka/vka.h>

#include <stddef.h>

#include "bootinfo.h"

extern seL4_BootInfo* platform_sel4_bi;

#define ALLOCMAN_BOOTSTRAP_POOL_SIZE (80 * 1024 * 1024)

static simple_t sel4_simple;
static allocman_t *sel4_allocman;
static vka_t sel4_vka;

static IPageAllocator *page_allocator;
static IPageAllocator page_alloc_instance;

size_t sel4_page_alloc_get_page_size(void* self) {
	// temporary hack
	return 4096;
}

int sel4_page_alloc_alloc(void* self, size_t count, void **pages) {
	return -1;
}

int sel4_page_alloc_free(void* self, size_t count, void *base) {
	return -1;
}

IPageAllocator* sel4_page_allocator_create(vka_t *vka) {
	klog(LOG_DEBUG,"sel4_page_allocator_create()\n");
	IPageAllocator *retval = &page_alloc_instance;
	retval->self = (void*)retval;
	retval->priv = (void*)vka;
	retval->get_page_size = &sel4_page_alloc_get_page_size;
	retval->alloc         = &sel4_page_alloc_alloc;
	retval->free          = &sel4_page_alloc_free;
	return retval;
}

void platform_init_early(void) {
}

void* allocman_pool;
void platform_init(void) {
     klog(LOG_INFO,"platform_init() for seL4\n");
     platform_sel4_bi = platform_sel4_get_bootinfo();
     platform_sel4_bootinfo_dump(platform_sel4_bi);

     klog(LOG_DEBUG, "platform_init() - setting up simple....\n");

     klog(LOG_DEBUG,"platform_init() - allocating %zu for allocman_pool\n", ALLOCMAN_BOOTSTRAP_POOL_SIZE);
     simple_default_init_bootinfo(&sel4_simple, platform_sel4_bi);

     allocman_pool = kmalloc(ALLOCMAN_BOOTSTRAP_POOL_SIZE);
     klog(LOG_DEBUG,"platform_init() - allocated pool at %p\n",allocman_pool);     

     if(allocman_pool==NULL) kpanic("Unable to allocate allocman bootstrap pool!");

     klog(LOG_DEBUG,"platform_init() - setting up allocman\n");
     sel4_allocman = bootstrap_use_current_simple(
		&sel4_simple,
		ALLOCMAN_BOOTSTRAP_POOL_SIZE,
		allocman_pool);

     if (sel4_allocman == NULL) kpanic("Unable to bootstrap seL4 allocman!");

     klog(LOG_DEBUG,"platform_init() - setting up VKA\n");
     allocman_make_vka(&sel4_vka, sel4_allocman);

     klog(LOG_DEBUG,"platform_init() - setting up IPageAllocator\n");
     page_allocator = sel4_page_allocator_create(&sel4_vka);
     if(page_allocator == NULL) kpanic("Unable to create page allocator instance!");

     kmalloc_init(page_allocator);
     klog(LOG_INFO,"seL4 platform ready\n");
}

void platform_yield(void) {
     seL4_Yield();
}
