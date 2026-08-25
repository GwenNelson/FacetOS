#include <unistd.h>

int main(int argc, const char *const *argv, const char *const *envp)
{
    (void)argc;
    (void)argv;
    (void)envp;
    static const char message[] = "hello from POSIX /bin/login\n";
    return write(1, message, sizeof(message) - 1) == sizeof(message) - 1
        ? 0 : 1;
}
