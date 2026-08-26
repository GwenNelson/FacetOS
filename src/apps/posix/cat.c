#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char buffer[128];
    int status = 0;
    if (argc < 2) {
        (void)write(2, "cat: missing operand\n", 21);
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            (void)write(2, "cat: cannot open ", 17);
            (void)write(2, argv[i], strlen(argv[i]));
            (void)write(2, "\n", 1);
            status = 1;
            continue;
        }
        ssize_t count;
        while ((count = read(fd, buffer, sizeof(buffer))) > 0)
            if (write(1, buffer, count) != count) { status = 1; break; }
        if (count < 0) status = 1;
        (void)close(fd);
    }
    return status;
}
