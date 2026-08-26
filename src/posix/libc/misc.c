#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>

size_t strnlen(const char *text, size_t maximum)
{
    size_t length = 0;
    while (length != maximum && text[length] != 0) length++;
    return length;
}

size_t strcspn(const char *text, const char *reject)
{
    size_t length = 0;
    for (; text[length] != 0; length++)
        for (const char *needle = reject; *needle != 0; needle++)
            if (text[length] == *needle) return length;
    return length;
}

unsigned long strtoul(const char *text, char **end, int base)
{
    unsigned long value = 0;
    if (base == 0) base = 10;
    while (*text >= '0' && *text <= '9') {
        value = value * (unsigned)base + (unsigned)(*text - '0');
        text++;
    }
    if (end != NULL) *end = (char *)text;
    return value;
}

int gethostname(char *name, size_t size)
{
    uint64_t domain = 0;
    IPOSIXView *view = facet_posix_view();
    if (name == NULL || view == NULL ||
        view->get_domain_id(view->self, &domain) != FACET_OK) {
        errno = EIO;
        return -1;
    }
    char digits[24];
    size_t count = 0;
    do { digits[count++] = (char)('0' + domain % 10); domain /= 10; } while (domain != 0);
    if (size <= count + sizeof("domain-") - 1) { errno = ENAMETOOLONG; return -1; }
    memcpy(name, "domain-", sizeof("domain-") - 1);
    for (size_t i = 0; i < count; i++) name[sizeof("domain-") - 1 + i] = digits[count - i - 1];
    name[sizeof("domain-") - 1 + count] = 0;
    return 0;
}

char *ttyname(int fd)
{
    const char *configured = getenv("FACET_TERMINAL");
    static char value[128];
    if (fd < 0 || fd > 2) { errno = ENOTTY; return NULL; }
    if (configured == NULL || strlen(configured) >= sizeof(value)) {
        errno = ENOTTY;
        return NULL;
    }
    strcpy(value, configured);
    return value;
}

int setuid(uid_t uid)
{
    IPOSIXView *view = facet_posix_view();
    int32_t error = 0;
    FacetResult result = view == NULL ? FACET_INVALID_HANDLE :
        view->set_credentials(view->self, (uint32_t)uid, UINT32_MAX, &error);
    if (result != FACET_OK || error != 0) {
        errno = error == 0 ? EIO : error;
        return -1;
    }
    return 0;
}

int setgid(gid_t gid)
{
    IPOSIXView *view = facet_posix_view();
    int32_t error = 0;
    FacetResult result = view == NULL ? FACET_INVALID_HANDLE :
        view->set_credentials(view->self, UINT32_MAX, (uint32_t)gid, &error);
    if (result != FACET_OK || error != 0) {
        errno = error == 0 ? EIO : error;
        return -1;
    }
    return 0;
}
