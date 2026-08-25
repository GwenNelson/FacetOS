#include <facetos/dominit/platform/api.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/IProcessEnvironment.h>

#include <stdint.h>
#include <string.h>

#ifndef FACET_DUMMY_MESSAGE
#define FACET_DUMMY_MESSAGE "FacetDummy shell override selected\r\n"
#endif

int main(int argc, char **argv)
{
    if (libfacet_register_generic_metadata(&IGenericObject_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IProcessEnvironment_MetaData) != FACET_OK ||
        libfacet_register_interface_metadata(&IByteWriter_MetaData) != FACET_OK)
        return 1;
    IGenericObject *root = NULL;
    if (platform_init(&argc, &argv, &root) != FACET_OK) return 1;
    IProcessEnvironment *environment =
        (IProcessEnvironment *)libfacet_proxy_client_get_interface(
            root, IID_IProcessEnvironment);
    FacetString stdout_name = {.data = "stdout", .length = 6};
    FacetHandle stdout_handle = {0};
    if (environment == NULL ||
        environment->resolve(environment->self, &stdout_name,
                             &stdout_handle) != FACET_OK)
        return 1;
    IByteWriter *output = libfacet_proxy_from_handle(&IByteWriter_MetaData,
                                                     stdout_handle);
    static const char message[] = FACET_DUMMY_MESSAGE;
    FacetArray_u8 bytes = {
        .data = (uint8_t *)(uintptr_t)message,
        .count = sizeof(message) - 1,
    };
    uint32_t written = 0;
    if (output == NULL ||
        output->write_bytes(output->self, &bytes, &written) != FACET_OK ||
        written != bytes.count)
        return 1;
    return 0;
}
