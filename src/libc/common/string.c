#include <stddef.h>
#include <stdlib.h>

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') length++;
    return length;
}

int strcmp(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) { left++; right++; }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t count)
{
    while (count != 0 && *left != '\0' && *left == *right) {
        left++; right++; count--;
    }
    return count == 0 ? 0 : (unsigned char)*left - (unsigned char)*right;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    do { *destination++ = *source; } while (*source++ != '\0');
    return result;
}

char *strchr(const char *text, int character)
{
    char wanted = (char)character;
    do { if (*text == wanted) return (char *)text; } while (*text++ != '\0');
    return NULL;
}

char *strdup(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy == NULL) return NULL;
    for (size_t i = 0; i < length; i++) copy[i] = text[i];
    return copy;
}
