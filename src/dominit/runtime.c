/* Minimal C runtime support required internally by sel4runtime. */
char *
strcpy(char *destination, const char *source)
{
    char *result = destination;

    do {
        *destination++ = *source;
    } while (*source++ != '\0');

    return result;
}
