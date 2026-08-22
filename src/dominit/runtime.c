/* Small standalone runtime for dominit and libfacet-common. */
#include <stddef.h>
#include <stdint.h>

#define DOMINIT_HEAP_SIZE (256u * 1024u)

typedef struct dominit_block {
    size_t size;
} dominit_block_t;

static union {
    max_align_t alignment;
    unsigned char bytes[DOMINIT_HEAP_SIZE];
} dominit_heap;
static size_t dominit_heap_used;

static size_t align_up(size_t value)
{
    const size_t alignment = sizeof(max_align_t);
    size_t mask = alignment - 1;
    if (value > SIZE_MAX - mask) return 0;
    return (value + mask) & ~mask;
}

void *malloc(size_t size)
{
    if (size == 0 || size > SIZE_MAX - sizeof(dominit_block_t))
        return NULL;

    size_t start = align_up(dominit_heap_used);
    size_t total = align_up(sizeof(dominit_block_t) + size);
    if (total == 0 || start > DOMINIT_HEAP_SIZE - total)
        return NULL;

    dominit_block_t *block =
        (dominit_block_t *)(void *)(dominit_heap.bytes + start);
    block->size = size;
    dominit_heap_used = start + total;
    return (void *)(block + 1);
}

void free(void *pointer)
{
    (void)pointer;
}

void *calloc(size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count)
        return NULL;

    size_t total = count * size;
    unsigned char *result = malloc(total);
    if (result == NULL && total != 0)
        return NULL;
    for (size_t i = 0; i < total; i++)
        result[i] = 0;
    return result;
}

void *realloc(void *pointer, size_t size)
{
    if (pointer == NULL) return malloc(size);
    if (size == 0) return NULL;

    dominit_block_t *old = ((dominit_block_t *)pointer) - 1;
    void *result = malloc(size);
    if (result == NULL) return NULL;

    size_t copy = old->size < size ? old->size : size;
    unsigned char *source = pointer;
    unsigned char *destination = result;
    for (size_t i = 0; i < copy; i++)
        destination[i] = source[i];
    return result;
}

void *memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *d = destination;
    const unsigned char *s = source;
    for (size_t i = 0; i < size; i++) d[i] = s[i];
    return destination;
}

void *memset(void *destination, int value, size_t size)
{
    unsigned char *d = destination;
    for (size_t i = 0; i < size; i++) d[i] = (unsigned char)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t size)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t i = 0; i < size; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *source, int value, size_t size)
{
    const unsigned char *s = source;
    for (size_t i = 0; i < size; i++)
        if (s[i] == (unsigned char)value) return (void *)(s + i);
    return NULL;
}

size_t strlen(const char *string)
{
    size_t length = 0;
    while (string[length] != '\0') length++;
    return length;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    do {
        *destination++ = *source;
    } while (*source++ != '\0');
    return result;
}
