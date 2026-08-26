#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static void append(char *buffer, size_t size, size_t *used, char byte)
{
    if (*used + 1 < size) buffer[*used] = byte;
    (*used)++;
}

int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
    size_t used = 0;
    while (*format != '\0') {
        if (*format != '%') { append(buffer, size, &used, *format++); continue; }
        format++;
        char padding = ' ';
        if (*format == '0') { padding = '0'; format++; }
        unsigned int width = 0;
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (unsigned int)(*format++ - '0');
        int long_long = 0;
        if (format[0] == 'l' && format[1] == 'l') { long_long = 1; format += 2; }
        char conversion = *format == '\0' ? '\0' : *format++;
        if (conversion == '%') { append(buffer, size, &used, '%'); continue; }
        if (conversion == 's') {
            const char *text = va_arg(arguments, const char *);
            while (*text != '\0') append(buffer, size, &used, *text++);
            continue;
        }
        unsigned int base = conversion == 'o' ? 8 : 10;
        unsigned long long value = long_long ?
            va_arg(arguments, unsigned long long) :
            (unsigned long long)va_arg(arguments, unsigned int);
        char digits[32];
        unsigned int count = 0;
        do {
            digits[count++] = (char)('0' + value % base);
            value /= base;
        } while (value != 0);
        while (width > count) { append(buffer, size, &used, padding); width--; }
        while (count != 0) append(buffer, size, &used, digits[--count]);
    }
    if (size != 0) buffer[used < size ? used : size - 1] = '\0';
    return (int)used;
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}
