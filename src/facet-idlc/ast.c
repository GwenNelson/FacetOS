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
    const char *return_type,
    const char *name)
{
    if (context->interface.method_count >= FACET_IDL_MAX_METHODS) {
        return NULL;
    }
    FacetIdlMethod *method = &context->interface.methods[
        context->interface.method_count++];
    memset(method, 0, sizeof(*method));
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
    copy_name(property->type, sizeof(property->type), type);
    copy_name(property->name, sizeof(property->name), name);
    property->readable = readable;
    property->writable = writable;

    char accessor[FACET_IDL_MAX_NAME];
    if (readable) {
        snprintf(accessor, sizeof(accessor), "get%s", name);
        if (facet_idl_add_method(context, "FacetResult", accessor) == NULL) {
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
        if (facet_idl_add_method(context, "FacetResult", accessor) == NULL) {
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

FacetIdlTypeDecl *facet_idl_add_type(
    FacetIdlContext *context,
    FacetIdlTypeKind kind,
    const char *name,
    const char *underlying_type)
{
    if (context->interface.type_count >= FACET_IDL_MAX_TYPES) return NULL;
    FacetIdlTypeDecl *type = &context->interface.types[
        context->interface.type_count++];
    memset(type, 0, sizeof(*type));
    type->kind = kind;
    copy_name(type->name, sizeof(type->name), name);
    copy_name(type->underlying_type, sizeof(type->underlying_type),
              underlying_type == NULL ? "i32" : underlying_type);
    return type;
}

int facet_idl_add_field(
    FacetIdlTypeDecl *type,
    const char *field_type,
    const char *name)
{
    if (type == NULL || type->field_count >= FACET_IDL_MAX_FIELDS) return -1;
    FacetIdlField *field = &type->fields[type->field_count++];
    copy_name(field->type, sizeof(field->type), field_type);
    copy_name(field->name, sizeof(field->name), name);
    return 0;
}

int facet_idl_add_enum_value(
    FacetIdlTypeDecl *type,
    const char *name,
    int64_t value)
{
    if (type == NULL || type->enum_value_count >= FACET_IDL_MAX_ENUM_VALUES) {
        return -1;
    }
    FacetIdlEnumValue *entry = &type->enum_values[type->enum_value_count++];
    copy_name(entry->name, sizeof(entry->name), name);
    entry->value = value;
    return 0;
}
