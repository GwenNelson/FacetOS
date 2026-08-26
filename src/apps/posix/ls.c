#include <facet_posix_runtime.h>
#include <facetos/interfaces/IPOSIXView.h>
#include <string.h>
#include <unistd.h>

static int list(const char *path)
{
    FacetString p = {.data = path, .length = strlen(path)};
    FacetArray_string entries = {0}; int32_t error = 0;
    IPOSIXView *view = facet_posix_view();
    if (!view || view->list_directory(view->self, &p, &entries, &error) != FACET_OK || error)
        return 1;
    for (size_t i = 0; i < entries.count; i++) {
        (void)write(1, entries.data[i].data, entries.data[i].length);
        (void)write(1, "\n", 1);
    }
    facet_rpc_release_value(FACET_TYPE_ARRAY, &FacetArray_string_TypeMeta, &entries);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 1) return list(".");
    int status = 0;
    for (int i = 1; i < argc; i++) if (list(argv[i]) != 0) status = 1;
    return status;
}
