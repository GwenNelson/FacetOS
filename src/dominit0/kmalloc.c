#include <stddef.h>

#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/klock.h>
#include <facetos/dominit0/kpanic.h>

#include <facetos/interfaces/IFrameAllocator.h>

static unsigned char bootstrap_heap[KMALLOC_BOOTSTRAP_HEAP_SIZE]
	__attribute__((aligned(KMALLOC_ALIGNMENT)));
static klock_t kmalloc_lock = KLOCK_INITIALIZER;

static size_t bootstrap_heap_used;

typedef void* (*kmalloc_impl_fn)(size_t size);
typedef void  (*kfree_impl_fn)(void* p);

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

void kmalloc_init(IFrameAllocator *allocator) {
     (void)allocator;
     klog(LOG_ERROR,"kmalloc_init: not yet implemented!\n");
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
