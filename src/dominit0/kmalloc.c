#include <stddef.h>
#include <string.h>

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
static unsigned char kmalloc_in_progress;

static size_t bootstrap_heap_used;

// most recent allocation and the size
// ideally we'd want a small stack to do this properly, maybe later?
static void*  bootstrap_last_ptr;
static size_t bootstrap_last_size;
static size_t bootstrap_last_old_used;

typedef void* (*kmalloc_impl_fn)(size_t size);
typedef void  (*kfree_impl_fn)(void* p);
typedef void* (*krealloc_impl_fn)(void *p, size_t size);

static IPageAllocator* kmalloc_allocator;

static void *kmalloc_impl_early(size_t size) {
    size_t old_used = bootstrap_heap_used;

    size_t pos =
        (bootstrap_heap_used + KMALLOC_ALIGNMENT - 1)
        & ~(KMALLOC_ALIGNMENT - 1);

    if (pos > KMALLOC_BOOTSTRAP_HEAP_SIZE ||
        size > KMALLOC_BOOTSTRAP_HEAP_SIZE - pos) {
        return NULL;
    }

    void *ptr = &bootstrap_heap[pos];

    bootstrap_heap_used = pos + size;

    bootstrap_last_ptr      = ptr;
    bootstrap_last_size     = size;
    bootstrap_last_old_used = old_used;

    return ptr;
}

static void kfree_impl_early(void *p) {
    if (p != bootstrap_last_ptr)
        return;

    bootstrap_heap_used = bootstrap_last_old_used;

    bootstrap_last_ptr      = NULL;
    bootstrap_last_size     = 0;
    bootstrap_last_old_used = 0;
}

static void *krealloc_impl_early(void *p, size_t size) {
    if (p == NULL)
        return kmalloc_impl_early(size);

    if (size == 0) {
        kfree_impl_early(p);
        return NULL;
    }

    /*
     * For now we can only realloc the most recent allocation.
     */
    if (p != bootstrap_last_ptr)
        return NULL;

    /*
     * p itself is already aligned. We only need to make sure
     * the resized allocation still fits in the bootstrap heap.
     */
    size_t pos = (size_t)((unsigned char *)p - bootstrap_heap);

    if (size > KMALLOC_BOOTSTRAP_HEAP_SIZE - pos)
        return NULL;

    bootstrap_last_size = size;
    bootstrap_heap_used = pos + size;

    return p;
}

static kmalloc_impl_fn  kmalloc_impl  = kmalloc_impl_early;
static kfree_impl_fn    kfree_impl    = kfree_impl_early;
static krealloc_impl_fn krealloc_impl = krealloc_impl_early;

void kmalloc_init_early(void) {
     klog(LOG_INFO,"Starting early bootstrap heap...\n");
     klock_init(&kmalloc_lock);
     kmalloc_impl        = &kmalloc_impl_early;
     kfree_impl          = &kfree_impl_early;
     krealloc_impl       = &krealloc_impl_early;
     bootstrap_heap_used = 0;
     klog(LOG_INFO,"Early bootstrap heap ready!\n");
}

// used to reclaim the BSS heap
static void kmalloc_reclaim(void) {
       size_t pos = (bootstrap_heap_used + KMALLOC_ALIGNMENT - 1) & ~(KMALLOC_ALIGNMENT - 1);
       void *remaining = &bootstrap_heap[pos];
       size_t remaining_size = KMALLOC_BOOTSTRAP_HEAP_SIZE - pos;
       klog(LOG_DEBUG,"Reclaiming %zukb from early bootstrap\n",remaining_size/1024);       
       void* region=kmalloc_impl_early(remaining_size);
       if(region==NULL) {
	       klog(LOG_WARN,"Failed to reclaim bootstrap buffer!\n");
       } else {
	       liballoc_add_region(region,remaining_size);
	       klog(LOG_DEBUG,"Reclaimed %zukb, registered with liballoc @ %p\n",remaining_size/1024, region);
       }
}

void kmalloc_init(IPageAllocator *allocator) {
     klock_lock(&kmalloc_lock);
     __atomic_store_n(&kmalloc_in_progress, 1, __ATOMIC_RELEASE);
     klog(LOG_DEBUG,"Early bootstrap heap switching to IPageAllocator with %zukb free!\n", ((KMALLOC_BOOTSTRAP_HEAP_SIZE-bootstrap_heap_used)/1024));
     kmalloc_reclaim();
     kmalloc_allocator = allocator;
     kmalloc_impl      = &liballoc_malloc_impl;
     kfree_impl        = &liballoc_free_impl;
     krealloc_impl     = &liballoc_realloc_impl;
     klock_init(&kmalloc_liballoc_lock);
     __atomic_store_n(&kmalloc_in_progress, 0, __ATOMIC_RELEASE);
     klock_unlock(&kmalloc_lock);
}

void *kmalloc(size_t size) {
      klock_lock(&kmalloc_lock);
      __atomic_store_n(&kmalloc_in_progress, 1, __ATOMIC_RELEASE);
      void* retval = kmalloc_impl(size);
      __atomic_store_n(&kmalloc_in_progress, 0, __ATOMIC_RELEASE);
      klock_unlock(&kmalloc_lock);
      if(retval==NULL) {
	 kpanic("RAN OUT OF KERNEL MEMORY!\n");
      }
      return retval;
}

void kfree(void* ptr) {
     klock_lock(&kmalloc_lock);
     __atomic_store_n(&kmalloc_in_progress, 1, __ATOMIC_RELEASE);
     kfree_impl(ptr);
     __atomic_store_n(&kmalloc_in_progress, 0, __ATOMIC_RELEASE);
     klock_unlock(&kmalloc_lock);
}

void* krealloc(void* p, size_t size) {
      klock_lock(&kmalloc_lock);
      __atomic_store_n(&kmalloc_in_progress, 1, __ATOMIC_RELEASE);
      void* retval=krealloc_impl(p,size);
      __atomic_store_n(&kmalloc_in_progress, 0, __ATOMIC_RELEASE);
      klock_unlock(&kmalloc_lock);
      return retval;
}

int kmalloc_is_in_progress(void) {
      return __atomic_load_n(&kmalloc_in_progress, __ATOMIC_ACQUIRE) != 0;
}

/*
 * seL4 support libraries use the standard allocator interface.  Keep those
 * allocations on dominit0's heap instead of falling through to musl's
 * syscall-backed allocator, which has no syscall provider in the root task.
 */
void *malloc(size_t size) {
      return kmalloc(size);
}

void free(void *ptr) {
      if (ptr != NULL)
          kfree(ptr);
}

void *calloc(size_t count, size_t size) {
      if (size != 0 && count > (size_t)-1 / size)
          return NULL;

      size_t bytes = count * size;
      void *ptr = kmalloc(bytes);
      return memset(ptr, 0, bytes);
}

void *realloc(void *ptr, size_t size) {
      return krealloc(ptr, size);
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
