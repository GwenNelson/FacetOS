#include <facetos/libc.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    errno = 0;
    assert(getauxval(AT_FACET_ABI_VERSION) == 0);
    assert(errno == ENOENT);

    char user[] = "USER=user";
    char home[] = "HOME=/home/user";
    char *environment[] = {user, home, NULL};
    FacetAuxvEntry auxiliary_vector[] = {
        {AT_FACET_ABI_VERSION, FACETOS_STARTUP_ABI_VERSION},
        {AT_FACET_ROOT_OBJECT, 1234},
        {0, 0},
    };
    facet_libc_initialize(environment, auxiliary_vector);
    assert(strcmp(getenv("USER"), "user") == 0);
    assert(strcmp(getenv("HOME"), "/home/user") == 0);
    assert(getenv("MISSING") == NULL);
    assert(getauxval(AT_FACET_ABI_VERSION) == FACETOS_STARTUP_ABI_VERSION);
    assert(getauxval(AT_FACET_ROOT_OBJECT) == 1234);

    assert(facet_auxv_validate(2, auxiliary_vector) == 0);
    FacetAuxvEntry duplicate[] = {
        {AT_FACET_ROOT_OBJECT, 1}, {AT_FACET_ROOT_OBJECT, 2},
    };
    assert(facet_auxv_validate(2, duplicate) != 0);
    FacetAuxvEntry standard = {3, 4096};
    assert(facet_auxv_validate(1, &standard) != 0);
    FacetAuxvEntry above = {AT_HIOS + 1, 0};
    assert(facet_auxv_validate(1, &above) != 0);
    assert(facet_auxv_validate(1, NULL) != 0);
    return 0;
}
