#include "ast.h"

#include <stdio.h>
#include <string.h>

static void copy_name(char *destination, size_t size, const char *source)
{
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, size, "%s", source);
}

void facet_idl_context_init(FacetIdlContext *context)
{
    memset(context, 0, sizeof(*context));
}

int facet_idl_add_required(FacetIdlContext *context, const char *name)
{
    if (context->interface.required_count >= FACET_IDL_MAX_REQUIRED) {
        return -1;
    }
    copy_name(context->interface.required[
                  context->interface.required_count++],
              FACET_IDL_MAX_NAME, name);
    return 0;
}

FacetIdlMethod *facet_idl_add_method(
    FacetIdlContext *context,
    uint32_t id,
    const char *return_type,
    const char *name)
{
    if (context->interface.method_count >= FACET_IDL_MAX_METHODS) {
        return NULL;
    }
    FacetIdlMethod *method = &context->interface.methods[
        context->interface.method_count++];
    memset(method, 0, sizeof(*method));
    method->id = id;
    copy_name(method->return_type, sizeof(method->return_type), return_type);
    copy_name(method->name, sizeof(method->name), name);
    return method;
}

int facet_idl_add_param(
    FacetIdlMethod *method,
    FacetIdlDirection direction,
    const char *type,
    const char *name)
{
    if (method == NULL || method->parameter_count >= FACET_IDL_MAX_PARAMS) {
        return -1;
    }
    FacetIdlParam *parameter = &method->parameters[
        method->parameter_count++];
    copy_name(parameter->type, sizeof(parameter->type), type);
    copy_name(parameter->name, sizeof(parameter->name), name);
    parameter->direction = direction;
    return 0;
}

int facet_idl_add_property(
    FacetIdlContext *context,
    uint32_t id,
    const char *type,
    const char *name,
    int readable,
    int writable)
{
    if (context->interface.property_count >= FACET_IDL_MAX_METHODS) {
        return -1;
    }
    FacetIdlProperty *property = &context->interface.properties[
        context->interface.property_count++];
    memset(property, 0, sizeof(*property));
    property->id = id;
    copy_name(property->type, sizeof(property->type), type);
    copy_name(property->name, sizeof(property->name), name);
    property->readable = readable;
    property->writable = writable;

    char accessor[FACET_IDL_MAX_NAME];
    if (readable) {
        snprintf(accessor, sizeof(accessor), "get%s", name);
        if (facet_idl_add_method(context, id, "FacetResult", accessor) == NULL) {
            return -1;
        }
        FacetIdlMethod *method = &context->interface.methods[
            context->interface.method_count - 1];
        if (facet_idl_add_param(method, FACET_IDL_OUT, type, "value") != 0) {
            return -1;
        }
    }
    if (writable) {
        snprintf(accessor, sizeof(accessor), "set%s", name);
        uint32_t setter_id = id + (readable ? 1u : 0u);
        if (facet_idl_add_method(context, setter_id, "FacetResult", accessor) == NULL) {
            return -1;
        }
        FacetIdlMethod *method = &context->interface.methods[
            context->interface.method_count - 1];
        if (facet_idl_add_param(method, FACET_IDL_IN, type, "value") != 0) {
            return -1;
        }
    }
    return 0;
}
