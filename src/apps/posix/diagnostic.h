#pragma once

#include <errno.h>
#include <string.h>
#include <unistd.h>

static inline const char *posix_error_message(int error)
{
    switch (error) {
    case EACCES:
        return "Permission denied";
    case ENOENT:
        return "No such file or directory";
    case ENOTDIR:
        return "Not a directory";
    case EISDIR:
        return "Is a directory";
    case EROFS:
        return "Read-only file system";
    default:
        return "I/O error";
    }
}

static inline void posix_path_error(const char *program, const char *path,
                                    int error)
{
    (void)write(2, program, strlen(program));
    (void)write(2, ": ", 2);
    (void)write(2, path, strlen(path));
    (void)write(2, ": ", 2);
    const char *message = posix_error_message(error);
    (void)write(2, message, strlen(message));
    (void)write(2, "\n", 1);
}
