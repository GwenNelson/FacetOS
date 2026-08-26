#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int list(const char *path)
{
    DIR *directory = opendir(path);
    if (directory == NULL) {
        /* A successful read-only open is enough to distinguish a regular
         * file operand from a missing path for this small ls implementation. */
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            (void)close(fd);
            (void)write(1, path, strlen(path));
            (void)write(1, "\n", 1);
            return 0;
        }
        (void)write(2, "ls: cannot access ", 18);
        (void)write(2, path, strlen(path));
        (void)write(2, "\n", 1);
        return 1;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        (void)write(1, entry->d_name, strlen(entry->d_name));
        (void)write(1, "\n", 1);
    }
    (void)closedir(directory);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 1) return list(".");
    int status = 0;
    for (int i = 1; i < argc; i++) if (list(argv[i]) != 0) status = 1;
    return status;
}
