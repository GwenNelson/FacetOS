#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
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
    IPOSIXView *view = facet_posix_view();
    static const char path[] = "/bin/login";
    static const char started[] = "FacetOS POSIX init (PID 1)\n";
    (void)write(1, started, sizeof(started) - 1);
    for (;;) {
        FacetString program = {.data = path, .length = sizeof(path) - 1};
        FacetString argv0 = program;
        FacetArray_string argv = {.data = &argv0, .count = 1};
        int32_t pid = -1, error = 0, status = 0;
        FacetResult result = view == NULL ? FACET_INVALID_HANDLE :
            view->spawn_inherited(view->self, &program, &argv, &pid, &error);
        if (result != FACET_OK || error != 0) {
            (void)write(1, "init: unable to start login (", 29);
            write_number((int)result);
            (void)write(1, "/", 1);
            write_number(error);
            (void)write(1, ")\n", 2);
            return 1;
        }
        while (view->wait_process(view->self, pid, &status, &error) == FACET_OK &&
               error != 0) facet_posix_yield();
    }
}
