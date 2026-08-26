#include "shell_parser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int facet_shell_parse_command(char *line, char *arguments[], size_t capacity,
                              size_t *count)
{
    if (line == NULL || arguments == NULL || count == NULL) return -1;
    char *source = line;
    char *destination = line;
    *count = 0;
    while (*source != '\0') {
        while (*source == ' ' || *source == '\t') source++;
        if (*source == '\0') break;
        if (*count == capacity) return -1;
        arguments[(*count)++] = destination;
        char quote = 0;
        for (;;) {
            char byte = *source;
            if (byte == '\0') {
                if (quote != 0) return -1;
                break;
            }
            if (byte == '\\') {
                source++;
                if (*source == '\0') return -1;
                *destination++ = *source++;
                continue;
            }
            if (quote != 0) {
                source++;
                if (byte == quote) quote = 0;
                else *destination++ = byte;
                continue;
            }
            if (byte == '\'' || byte == '"') {
                quote = byte;
                source++;
                continue;
            }
            if (byte == ' ' || byte == '\t') break;
            *destination++ = byte;
            source++;
        }
        while (*source == ' ' || *source == '\t') source++;
        *destination++ = '\0';
    }
    return 0;
}

char *facet_shell_path_candidate(const char *path, size_t *offset,
                                 const char *command)
{
    if (path == NULL || offset == NULL || command == NULL ||
        *offset == SIZE_MAX)
        return NULL;
    const char *cursor = path + *offset;
    const char *end = strchr(cursor, ':');
    size_t directory_length = end == NULL ? strlen(cursor) :
        (size_t)(end - cursor);
    const char *directory = directory_length == 0 ? "." : cursor;
    if (directory_length == 0) directory_length = 1;
    size_t needed = directory_length + 1 + strlen(command) + 1;
    char *candidate = malloc(needed);
    if (candidate == NULL) return NULL;
    memcpy(candidate, directory, directory_length);
    size_t used = directory_length;
    if (candidate[used - 1] != '/') candidate[used++] = '/';
    strcpy(&candidate[used], command);
    *offset = end == NULL ? SIZE_MAX : (size_t)(end - path) + 1;
    return candidate;
}
