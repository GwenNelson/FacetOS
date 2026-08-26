#pragma once

#include <stddef.h>

int facet_shell_parse_command(char *line, char *arguments[], size_t capacity,
                              size_t *count);
char *facet_shell_path_candidate(const char *path, size_t *offset,
                                 const char *command);
