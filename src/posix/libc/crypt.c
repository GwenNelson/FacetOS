#include <crypt.h>
#include <stdarg.h>
#include <string.h>

extern char *__crypt_sha256(const char *key, const char *setting, char *output);

/* crypt_sha256.c requires only this narrow sprintf form. */
int sprintf(char *output, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *cursor = output;
    for (; *format != 0; format++) {
        if (*format != '%') { *cursor++ = *format; continue; }
        format++;
        if (*format == 's') {
            const char *text = va_arg(args, const char *);
            size_t size = strlen(text);
            memcpy(cursor, text, size); cursor += size;
        } else if (*format == '.' && format[1] == '*' && format[2] == 's') {
            int count = va_arg(args, int);
            const char *text = va_arg(args, const char *);
            if (count > 0) { memcpy(cursor, text, (size_t)count); cursor += count; }
            format += 2;
        } else if (*format == 'u') {
            unsigned value = va_arg(args, unsigned);
            char digits[16];
            size_t count = 0;
            do {
                digits[count++] = (char)('0' + value % 10u);
                value /= 10u;
            } while (value != 0);
            while (count != 0) *cursor++ = digits[--count];
        }
    }
    *cursor = 0;
    va_end(args);
    return (int)(cursor - output);
}

char *crypt(const char *key, const char *setting)
{
    static char output[128];
    if (key == NULL || setting == NULL || setting[0] != '$' ||
        setting[1] != '5' || setting[2] != '$')
        return "*";
    return __crypt_sha256(key, setting, output);
}
