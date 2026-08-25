/* Small standalone libc primitives for dominit and libfacet-common.
 * Allocation lives in platform/allocator.c so it can switch to IPageAllocator. */
#include <stddef.h>
#include <stdint.h>

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
