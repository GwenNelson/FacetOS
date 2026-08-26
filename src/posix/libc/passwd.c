#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <shadow.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char passwd_buffer[512];
static char shadow_buffer[512];
static struct passwd passwd_entry;
static struct spwd shadow_entry;

static char *field(char **cursor)
{
    char *value = *cursor;
    char *end = strchr(value, ':');
    if (end == NULL) return NULL;
    *end = 0;
    *cursor = end + 1;
    return value;
}

static int load_line(const char *path, const char *name, char *buffer,
                     size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t count = read(fd, buffer, size - 1);
    (void)close(fd);
    if (count < 0) return -1;
    buffer[count] = 0;
    char *line = buffer;
    while (*line != 0) {
        char *next = strchr(line, '\n');
        if (next != NULL) *next = 0;
        size_t length = strcspn(line, ":");
        if (strlen(name) == length && memcmp(line, name, length) == 0) {
            if (line != buffer)
                memmove(buffer, line, strlen(line) + 1);
            return 0;
        }
        if (next == NULL) break;
        line = next + 1;
    }
    errno = ENOENT;
    return -1;
}

struct passwd *getpwnam(const char *name)
{
    if (name == NULL || load_line("/etc/passwd", name, passwd_buffer,
                                  sizeof(passwd_buffer)) != 0)
        return NULL;
    char *cursor = passwd_buffer;
    passwd_entry.pw_name = field(&cursor);
    passwd_entry.pw_passwd = field(&cursor);
    char *uid = field(&cursor);
    char *gid = field(&cursor);
    passwd_entry.pw_gecos = field(&cursor);
    passwd_entry.pw_dir = field(&cursor);
    passwd_entry.pw_shell = cursor;
    char *end = strchr(cursor, '\n');
    if (end != NULL) *end = 0;
    if (passwd_entry.pw_name == NULL || passwd_entry.pw_passwd == NULL ||
        uid == NULL || gid == NULL || passwd_entry.pw_gecos == NULL ||
        passwd_entry.pw_dir == NULL || passwd_entry.pw_shell == NULL) {
        errno = EIO;
        return NULL;
    }
    passwd_entry.pw_uid = (uid_t)strtoul(uid, NULL, 10);
    passwd_entry.pw_gid = (gid_t)strtoul(gid, NULL, 10);
    return &passwd_entry;
}

struct spwd *getspnam(const char *name)
{
    if (name == NULL || load_line("/etc/shadow", name, shadow_buffer,
                                  sizeof(shadow_buffer)) != 0)
        return NULL;
    char *cursor = shadow_buffer;
    shadow_entry.sp_namp = field(&cursor);
    shadow_entry.sp_pwdp = field(&cursor);
    if (shadow_entry.sp_namp == NULL || shadow_entry.sp_pwdp == NULL) {
        errno = EIO;
        return NULL;
    }
    return &shadow_entry;
}
