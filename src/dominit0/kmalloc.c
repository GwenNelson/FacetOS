#include <stddef.h>

#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/klock.h>
#include <facetos/dominit0/kpanic.h>

#include <facetos/interfaces/IPageAllocator.h>

#include <liballoc.h>

static unsigned char bootstrap_heap[KMALLOC_BOOTSTRAP_HEAP_SIZE]
	__attribute__((aligned(KMALLOC_ALIGNMENT)));
static klock_t kmalloc_lock          = KLOCK_INITIALIZER;
static klock_t kmalloc_liballoc_lock = KLOCK_INITIALIZER;

static size_t bootstrap_heap_used;

typedef void* (*kmalloc_impl_fn)(size_t size);
typedef void  (*kfree_impl_fn)(void* p);

static IPageAllocator* kmalloc_allocator;

static void* kmalloc_impl_early(size_t size) {
	size_t pos;
	void *ptr;

	pos = (bootstrap_heap_used + KMALLOC_ALIGNMENT - 1) & ~(KMALLOC_ALIGNMENT - 1);

	if (pos > KMALLOC_BOOTSTRAP_HEAP_SIZE || size > KMALLOC_BOOTSTRAP_HEAP_SIZE - pos) {
		return NULL;
	}

	ptr = &bootstrap_heap[pos];
	bootstrap_heap_used = pos + size;

	return ptr;
}

static void kfree_impl_early(void* p) {
       (void)p;
}

static void* kmalloc_impl_real(size_t size) {

}

static void kfree_impl_real(void* p) {
}

static kmalloc_impl_fn kmalloc_impl = kmalloc_impl_early;
static kfree_impl_fn   kfree_impl   = kfree_impl_early;

void kmalloc_init_early(void) {
     klog(LOG_INFO,"Starting early bootstrap heap...\n");
     klock_init(&kmalloc_lock);
     kmalloc_impl        = &kmalloc_impl_early;
     kfree_impl          = &kfree_impl_early;
     bootstrap_heap_used = 0;
     klog(LOG_INFO,"Early bootstrap heap ready!\n");
}

void kmalloc_init(IPageAllocator *allocator) {
     klock_lock(&kmalloc_lock);
     klog(LOG_DEBUG,"Early bootstrap heap switching to IPageAllocator with %zukb free!\n", ((KMALLOC_BOOTSTRAP_HEAP_SIZE-bootstrap_heap_used)/1024));
     // TODO - when we import liballoc, should probably add the remaining bootstrap heap to the liballoc heap
     //        but only after verifying that IPageAllocator is working
     kmalloc_allocator = allocator;
     kmalloc_impl      = &liballoc_malloc_impl;
     kfree_impl        = &liballoc_free_impl;
     klock_init(&kmalloc_liballoc_lock);
     klock_unlock(&kmalloc_lock);
}

void *kmalloc(size_t size) {
      klock_lock(&kmalloc_lock);
      void* retval = kmalloc_impl(size);
      klock_unlock(&kmalloc_lock);
      if(retval==NULL) {
	 kpanic("RAN OUT OF KERNEL MEMORY!\n");
      }
      return retval;
}

void kfree(void* ptr) {
     klock_lock(&kmalloc_lock);
     kfree_impl(ptr);
     klock_unlock(&kmalloc_lock);
}

void kmalloc_dump(void) {
     klog_lock();
     klog(LOG_DEBUG,"kmalloc_dump\n");
     if(kmalloc_impl == &kmalloc_impl_early) {
	klog(LOG_DEBUG,"\t Using early bootstrap still, %zukb used and %zukb free\n",(bootstrap_heap_used/1024),
										     ((KMALLOC_BOOTSTRAP_HEAP_SIZE-bootstrap_heap_used)/1024));
     } else {
	klog(LOG_DEBUG,"\t No longer using early bootstrap, %zukb of bootstrap heap for reclaiming\n", ((KMALLOC_BOOTSTRAP_HEAP_SIZE-bootstrap_heap_used)/1024));
     }
     klog(LOG_DEBUG,"\t IPageAllocator at %p\n",kmalloc_allocator);
     klog_unlock();
}

int liballoc_lock(void)
{
    klock_lock(&kmalloc_liballoc_lock);
    return 0;
}

int liballoc_unlock(void)
{
    klock_unlock(&kmalloc_liballoc_lock);
    return 0;
}

void *liballoc_alloc(int pages)
{
    void *base = NULL;

    if (kmalloc_allocator == NULL)
        return NULL;

    if (kmalloc_allocator->alloc(
            kmalloc_allocator->self,
            pages,
            &base) != 0)
        return NULL;

    return base;
}

int liballoc_free(void *ptr, int pages)
{
    if (kmalloc_allocator == NULL)
        return -1;

    return kmalloc_allocator->free(
        kmalloc_allocator->self,
        pages,
        ptr);
}
