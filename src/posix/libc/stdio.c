#include <stddef.h>
#include <string.h>
#include <unistd.h>

int putchar(int character)
{
    unsigned char byte = (unsigned char)character;
    return write(1, &byte, 1) == 1 ? character : -1;
}

int puts(const char *text)
{
    size_t length = strlen(text);
    return write(1, text, length) == (ssize_t)length &&
           write(1, "\n", 1) == 1 ? 0 : -1;
}
