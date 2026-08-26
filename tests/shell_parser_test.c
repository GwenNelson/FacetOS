#include "shell_parser.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void check_candidate(const char *path, size_t *offset,
                            const char *expected)
{
    char *candidate = facet_shell_path_candidate(path, offset, "ls");
    assert(candidate != NULL);
    assert(strcmp(candidate, expected) == 0);
    free(candidate);
}

int main(void)
{
    char line[] = "cat \"two words\" 'three words' escaped\\ value";
    char *arguments[8];
    size_t count = 0;
    assert(facet_shell_parse_command(line, arguments, 8, &count) == 0);
    assert(count == 4);
    assert(strcmp(arguments[0], "cat") == 0);
    assert(strcmp(arguments[1], "two words") == 0);
    assert(strcmp(arguments[2], "three words") == 0);
    assert(strcmp(arguments[3], "escaped value") == 0);

    char unmatched[] = "cat 'broken";
    assert(facet_shell_parse_command(unmatched, arguments, 8, &count) != 0);
    char dangling[] = "cat broken\\";
    assert(facet_shell_parse_command(dangling, arguments, 8, &count) != 0);
    char too_many[] = "one two";
    assert(facet_shell_parse_command(too_many, arguments, 1, &count) != 0);

    size_t offset = 0;
    check_candidate("/bin::/FacetOS/", &offset, "/bin/ls");
    check_candidate("/bin::/FacetOS/", &offset, "./ls");
    check_candidate("/bin::/FacetOS/", &offset, "/FacetOS/ls");
    assert(offset == SIZE_MAX);
    assert(facet_shell_path_candidate("/bin", &offset, "ls") == NULL);
    return 0;
}
