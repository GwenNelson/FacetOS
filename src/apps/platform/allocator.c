#include "allocator.h"

#include <liballoc.h>

#include <stddef.h>
#include <stdint.h>

#define APP_BOOTSTRAP_HEAP_SIZE (256u * 1024u)
#define APP_BOOTSTRAP_ALIGNMENT sizeof(max_align_t)
#define APP_LIBALLOC_PAGE_SIZE 4096u

typedef struct AppBootstrapBlock {
    size_t size;
} AppBootstrapBlock;

static union {
    max_align_t alignment;
    unsigned char bytes[APP_BOOTSTRAP_HEAP_SIZE];
} bootstrap_heap;

static size_t bootstrap_used;
static size_t bootstrap_handoff;
static IPageAllocator *page_allocator;
static unsigned int liballoc_locked;
static int allocator_active;

static void *bootstrap_malloc(size_t size);

static size_t align_up(size_t value)
{
    size_t mask = APP_BOOTSTRAP_ALIGNMENT - 1;
    if (value > SIZE_MAX - mask) return 0;
    return (value + mask) & ~mask;
}

static int is_early_pointer(const void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;
    uintptr_t start = (uintptr_t)bootstrap_heap.bytes;
    return address >= start && address < start + bootstrap_handoff;
}

static void *bootstrap_realloc(void *pointer, size_t size)
{
    AppBootstrapBlock *old = ((AppBootstrapBlock *)pointer) - 1;
    void *result = bootstrap_malloc(size);
    if (result == NULL) return NULL;
    size_t copy = old->size < size ? old->size : size;
    unsigned char *destination = result;
    unsigned char *source = pointer;
    for (size_t i = 0; i < copy; i++) destination[i] = source[i];
    return result;
}

static void *bootstrap_malloc(size_t size)
{
    if (size == 0 || size > SIZE_MAX - sizeof(AppBootstrapBlock)) return NULL;
    size_t start = align_up(bootstrap_used);
    size_t total = align_up(sizeof(AppBootstrapBlock) + size);
    if (total == 0 || start > APP_BOOTSTRAP_HEAP_SIZE - total) return NULL;
    AppBootstrapBlock *block =
        (AppBootstrapBlock *)(void *)(bootstrap_heap.bytes + start);
    block->size = size;
    bootstrap_used = start + total;
    return block + 1;
}

void *malloc(size_t size)
{
    return allocator_active ? liballoc_malloc_impl(size) :
                                   bootstrap_malloc(size);
}

void free(void *pointer)
{
    if (pointer == NULL || !allocator_active || is_early_pointer(pointer))
        return;
    liballoc_free_impl(pointer);
}

void *calloc(size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count) return NULL;
    size_t total = count * size;
    unsigned char *result = malloc(total);
    if (result == NULL && total != 0) return NULL;
    for (size_t i = 0; i < total; i++) result[i] = 0;
    return result;
}

void *realloc(void *pointer, size_t size)
{
    if (pointer == NULL) return malloc(size);
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    if (!allocator_active) return bootstrap_realloc(pointer, size);
    if (!is_early_pointer(pointer)) return liballoc_realloc_impl(pointer, size);

    AppBootstrapBlock *old = ((AppBootstrapBlock *)pointer) - 1;
    void *result = liballoc_malloc_impl(size);
    if (result == NULL) return NULL;
    size_t copy = old->size < size ? old->size : size;
    unsigned char *destination = result;
    unsigned char *source = pointer;
    for (size_t i = 0; i < copy; i++) destination[i] = source[i];
    return result;
}

int liballoc_lock(void)
{
    while (__atomic_test_and_set(&liballoc_locked, __ATOMIC_ACQUIRE)) { }
    return 0;
}

int liballoc_unlock(void)
{
    __atomic_clear(&liballoc_locked, __ATOMIC_RELEASE);
    return 0;
}

void *liballoc_alloc(int pages)
{
    if (page_allocator == NULL || pages <= 0) return NULL;
    void *base = NULL;
    if (page_allocator->alloc(page_allocator->self, (uint64_t)pages, &base) !=
        FACET_OK)
        return NULL;
    return base;
}

int liballoc_free(void *pointer, int pages)
{
    if (page_allocator == NULL || pointer == NULL || pages <= 0) return -1;
    return page_allocator->free(page_allocator->self, (uint64_t)pages,
                                (uint64_t)(uintptr_t)pointer) == FACET_OK ? 0 : -1;
}

static int activate_allocator(IPageAllocator *allocator)
{
    if (allocator_active) return -1;
    size_t handoff = align_up(bootstrap_used);
    if (handoff >= APP_BOOTSTRAP_HEAP_SIZE) return -1;
    page_allocator = allocator;
    bootstrap_handoff = handoff;
    if (liballoc_add_region(bootstrap_heap.bytes + handoff,
                            APP_BOOTSTRAP_HEAP_SIZE - handoff) != 0) {
        page_allocator = NULL;
        bootstrap_handoff = 0;
        return -1;
    }
    allocator_active = 1;
    return 0;
}

int facet_app_allocator_use_pages(IPageAllocator *allocator)
{
    if (allocator == NULL) return -1;
    uint64_t page_size = 0;
    if (allocator->get_page_size(allocator->self, &page_size) != FACET_OK ||
        page_size != APP_LIBALLOC_PAGE_SIZE)
        return -1;
    return activate_allocator(allocator);
}

int facet_app_allocator_use_static(void)
{
    return activate_allocator(NULL);
}
