#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

static void write_number(int value)
{
    char digits[12];
    unsigned length = 0;
    unsigned number = value < 0 ? (unsigned)-value : (unsigned)value;
    do { digits[length++] = (char)('0' + number % 10); number /= 10; } while (number);
    if (value < 0) (void)write(1, "-", 1);
    while (length != 0) (void)write(1, &digits[--length], 1);
}

int main(void)
{
    static const char path[] = "/bin/login";
    static const char started[] = "FacetOS POSIX init (PID 1)\n";
    (void)write(1, started, sizeof(started) - 1);
    for (;;) {
        char *const argv[] = {(char *)path, NULL};
        pid_t pid = -1;
        int status = 0;
        int spawn_error = posix_spawn(&pid, path, NULL, NULL, argv, NULL);
        if (spawn_error != 0) {
            (void)write(1, "init: unable to start login (", 29);
            write_number(spawn_error);
            (void)write(1, ")\n", 2);
            return 1;
        }
        if (waitpid(pid, &status, 0) != pid)
            return 1;
    }
}
