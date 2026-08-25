#include <facetos/dominit0/posix.h>

#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IPOSIXView.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct Dominit0PosixView {
    IPOSIXView interface;
    FacetHandle handle;
    FacetHandle stdout_handle;
};

static bool iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static FacetResult get_interface(void *self, uuid_t iid, FacetHandle *out)
{
    Dominit0PosixView *view = self;
    if (out == NULL) return FACET_INVALID_ARGUMENT;
    *out = (FacetHandle){0};
    if (!iid_equal(iid, IID_IGenericObject) &&
        !iid_equal(iid, IID_IPOSIXView))
        return FACET_NO_INTERFACE;
    *out = view->handle;
    return FACET_OK;
}

static FacetResult posix_write(void *self, int32_t fd,
                               const FacetArray_u8 *data,
                               int64_t *result, int32_t *error)
{
    Dominit0PosixView *view = self;
    if (data == NULL || result == NULL || error == NULL ||
        (data->count != 0 && data->data == NULL))
        return FACET_INVALID_ARGUMENT;
    *result = -1;
    *error = 0;
    if (fd != 1) {
        *error = EBADF;
        return FACET_OK;
    }
    IByteWriter *writer = libfacet_proxy_from_handle(
        &IByteWriter_MetaData, view->stdout_handle);
    if (writer == NULL) return FACET_INVALID_HANDLE;
    uint32_t written = 0;
    FacetResult transport = writer->write_bytes(writer->self, data, &written);
    libfacet_free_proxy_client(writer);
    if (transport != FACET_OK) return transport;
    *result = written;
    return FACET_OK;
}

Dominit0PosixView *dominit0_posix_view_create(FacetHandle stdout_handle)
{
    if (stdout_handle.platform == NULL) return NULL;
    Dominit0PosixView *view = calloc(1, sizeof(*view));
    if (view == NULL) return NULL;
    view->stdout_handle = stdout_handle;
    view->interface = (IPOSIXView){
        .self = view,
        .priv = view,
        .getInterface = get_interface,
        .write_fd = posix_write,
    };
    if (libfacet_export_interface(&view->interface, &IPOSIXView_MetaData,
                                  &view->handle) != FACET_OK) {
        free(view);
        return NULL;
    }
    return view;
}

FacetHandle dominit0_posix_view_handle(const Dominit0PosixView *view)
{
    return view == NULL ? (FacetHandle){0} : view->handle;
}

void dominit0_posix_view_destroy(Dominit0PosixView *view)
{
    if (view == NULL) return;
    if (view->handle.platform != NULL)
        (void)libfacet_unexport_interface(view->handle);
    free(view);
}
