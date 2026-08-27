#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "diagnostic.h"

static void text(int fd, const char *value)
{
    (void)write(fd, value, strlen(value));
}

static int line_prefix(unsigned long long value)
{
    char digits[32];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    for (unsigned padding = count; padding < 6; padding++)
        if (write(1, " ", 1) != 1) return -1;
    while (count != 0)
        if (write(1, &digits[--count], 1) != 1) return -1;
    return write(1, "\t", 1) == 1 ? 0 : -1;
}

static int copy_fd(int fd, bool numbered, unsigned long long *line_number)
{
    char buffer[128];
    bool line_start = true;
    for (;;) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) return 1;
        if (count == 0) {
            if (fd == 0) continue;
            return 0;
        }
        for (ssize_t i = 0; i < count; i++) {
            if (numbered && line_start) {
                if (line_prefix((*line_number)++) != 0) return 1;
                line_start = false;
            }
            if (write(1, &buffer[i], 1) != 1) return 1;
            if (buffer[i] == '\n') line_start = true;
        }
    }
}

int main(int argc, char **argv)
{
    bool numbered = false, options = true;
    int first = argc;
    for (int i = 1; i < argc; i++) {
        if (options && strcmp(argv[i], "--") == 0) {
            options = false;
            continue;
        }
        if (options && strcmp(argv[i], "-n") == 0) {
            numbered = true;
            continue;
        }
        first = i;
        break;
    }

    unsigned long long line_number = 1;
    if (first == argc) return copy_fd(0, numbered, &line_number);
    int status = 0;
    for (int i = first; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            status |= copy_fd(0, numbered, &line_number);
            continue;
        }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            int error = errno;
            posix_path_error("cat", argv[i], error);
            status = 1;
            continue;
        }
        int copied = copy_fd(fd, numbered, &line_number);
        if (copied != 0) {
            int error = errno;
            posix_path_error("cat", argv[i], error);
            status = 1;
        }
        if (close(fd) != 0) status = 1;
    }
    return status;
}
