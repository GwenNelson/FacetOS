#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int readable(const char *path)
{
    char buffer[32];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    int result = read(fd, buffer, sizeof(buffer)) > 0;
    if (lseek(fd, 0, SEEK_SET) != 0) result = 0;
    if (close(fd) != 0) result = 0;
    return result;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts("POSIX test_perms: START");
    char *allocation = malloc(8192);
    int ok = allocation != NULL;
    if (allocation != NULL) {
        memset(allocation, 0x5a, 8192);
        free(allocation);
    }
    const char *user = getenv("USER");
    const char *home = getenv("HOME");
    ok = ok && user != NULL && strcmp(user, "user") == 0 &&
         home != NULL && strcmp(home, "/home/user") == 0;
    ok = ok && readable("/usr/share/test_data/public.txt");
    ok = ok && readable("/usr/share/test_data/user-only.txt");
    errno = 0;
    ok = ok && !readable("/usr/share/test_data/root-only.txt") &&
         errno == EACCES;
    errno = 0;
    ok = ok && open("/usr/share/test_data/missing", O_RDONLY) < 0 &&
         errno == ENOENT;
    errno = 0;
    ok = ok && open("/usr/share/test_data/public.txt", O_WRONLY) < 0 &&
         errno == EROFS;
    puts(ok ? "POSIX test_perms: PASS" : "POSIX test_perms: FAIL");
    return ok ? 0 : 1;
}
