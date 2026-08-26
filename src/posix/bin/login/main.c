#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static const char message[] = "hello from POSIX /bin/login\n";
    return write(1, message, sizeof(message) - 1) == sizeof(message) - 1
        ? 0 : 1;
}
