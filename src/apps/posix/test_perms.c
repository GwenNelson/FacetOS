#include <errno.h>
#include <dirent.h>
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

static int directory_fails(const char *path, int expected)
{
    errno = 0;
    DIR *directory = opendir(path);
    int error = errno;
    if (directory != NULL) (void)closedir(directory);
    return directory == NULL && error == expected;
}

static int chdir_fails(const char *path, int expected)
{
    errno = 0;
    int result = chdir(path);
    int error = errno;
    return result < 0 && error == expected;
}

static int read_fails(const char *path, int expected)
{
    errno = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return errno == expected;
    char byte = 0;
    int result = read(fd, &byte, 1);
    int error = errno;
    (void)close(fd);
    return result < 0 && error == expected;
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
    ok = ok && read_fails("/usr/share/test_data/root-only.txt", EACCES);
    errno = 0;
    ok = ok && open("/usr/share/test_data/missing", O_RDONLY) < 0 &&
         errno == ENOENT;
    ok = ok &&
         chdir_fails("/usr/share/test_data/root-private", EACCES) &&
         chdir_fails("/usr/share/test_data/missing-directory", ENOENT) &&
         directory_fails("/usr/share/test_data/root-private", EACCES) &&
         directory_fails("/usr/share/test_data/missing-directory", ENOENT);
    errno = 0;
    ok = ok && open("/usr/share/test_data/public.txt", O_WRONLY) < 0 &&
         errno == EROFS;
    puts(ok ? "POSIX test_perms: PASS" : "POSIX test_perms: FAIL");
    return ok ? 0 : 1;
}
