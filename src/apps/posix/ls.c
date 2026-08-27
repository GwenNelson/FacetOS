#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "diagnostic.h"

static void text(int fd, const char *value)
{
    (void)write(fd, value, strlen(value));
}

static void number(unsigned long long value, unsigned base, unsigned width)
{
    char digits[32];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % base);
        value /= base;
    } while (value != 0);
    while (count < width) digits[count++] = '0';
    while (count != 0) (void)write(1, &digits[--count], 1);
}

static void metadata(const struct stat *value)
{
    number((unsigned)value->st_mode, 8, 6);
    text(1, " ");
    number((unsigned)value->st_uid, 10, 1);
    text(1, " ");
    number((unsigned)value->st_gid, 10, 1);
    text(1, " ");
}

static int joined_path(char *result, size_t capacity, const char *directory,
                       const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    bool slash = directory_length != 0 &&
        directory[directory_length - 1] != '/';
    if (directory_length + (slash ? 1 : 0) + name_length + 1 > capacity)
        return -1;
    memcpy(result, directory, directory_length);
    if (slash) result[directory_length++] = '/';
    memcpy(result + directory_length, name, name_length + 1);
    return 0;
}

static int list_one(const char *path, bool long_format, bool show_all,
                    bool heading)
{
    DIR *directory = opendir(path);
    if (directory == NULL) {
        int open_error = errno;
        struct stat value;
        if (stat(path, &value) == 0 && !S_ISDIR(value.st_mode)) {
            if (long_format) metadata(&value);
            text(1, path);
            text(1, "\n");
            return 0;
        }
        posix_path_error("ls", path, open_error);
        return 1;
    }
    if (heading) {
        text(1, path);
        text(1, ":\n");
    }
    int status = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (!show_all && entry->d_name[0] == '.') continue;
        char child[512];
        struct stat value;
        bool have_metadata = joined_path(child, sizeof(child), path,
                                         entry->d_name) == 0 &&
            stat(child, &value) == 0;
        if (long_format) {
            if (have_metadata) metadata(&value);
            else {
                struct stat fallback = {.st_mode = 0100644};
                metadata(&fallback);
                status = 1;
            }
        }
        text(1, entry->d_name);
        if (have_metadata && S_ISDIR(value.st_mode)) text(1, "/");
        text(1, "\n");
    }
    if (closedir(directory) != 0) status = 1;
    return status;
}

int main(int argc, char **argv)
{
    bool long_format = false, show_all = false, options = true;
    int first_path = argc;
    for (int i = 1; i < argc; i++) {
        if (options && strcmp(argv[i], "--") == 0) {
            options = false;
            continue;
        }
        if (options && argv[i][0] == '-' && argv[i][1] != '\0') {
            for (const char *flag = argv[i] + 1; *flag != '\0'; flag++) {
                if (*flag == 'l') long_format = true;
                else if (*flag == 'a') show_all = true;
                else {
                    text(2, "ls: invalid option\n");
                    return 1;
                }
            }
            continue;
        }
        first_path = i;
        break;
    }
    if (first_path == argc)
        return list_one(".", long_format, show_all, false);
    int status = 0;
    for (int i = first_path; i < argc; i++) {
        if (i != first_path) text(1, "\n");
        if (list_one(argv[i], long_format, show_all,
                     argc - first_path > 1) != 0)
            status = 1;
    }
    return status;
}
