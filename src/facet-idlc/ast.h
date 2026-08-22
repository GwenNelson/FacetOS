#pragma once

#include <stddef.h>
#include <stdint.h>

#define FACET_IDL_MAX_NAME 96
#define FACET_IDL_MAX_PARAMS 32
#define FACET_IDL_MAX_METHODS 128
#define FACET_IDL_MAX_REQUIRED 64
#define FACET_IDL_MAX_TYPES 128
#define FACET_IDL_MAX_FIELDS 64
#define FACET_IDL_MAX_ENUM_VALUES 128

typedef enum FacetIdlDirection {
    FACET_IDL_IN,
    FACET_IDL_OUT,
    FACET_IDL_INOUT,
} FacetIdlDirection;

typedef struct FacetIdlParam {
    char type[FACET_IDL_MAX_NAME];
    char name[FACET_IDL_MAX_NAME];
    FacetIdlDirection direction;
} FacetIdlParam;

typedef struct FacetIdlMethod {
    uint32_t id;
    char name[FACET_IDL_MAX_NAME];
    char return_type[FACET_IDL_MAX_NAME];
    size_t parameter_count;
    FacetIdlParam parameters[FACET_IDL_MAX_PARAMS];
} FacetIdlMethod;

typedef struct FacetIdlProperty {
    uint32_t id;
    char type[FACET_IDL_MAX_NAME];
    char name[FACET_IDL_MAX_NAME];
    int readable;
    int writable;
} FacetIdlProperty;

typedef enum FacetIdlTypeKind {
    FACET_IDL_TYPE_ENUM,
    FACET_IDL_TYPE_STRUCT,
} FacetIdlTypeKind;

typedef struct FacetIdlField {
    char type[FACET_IDL_MAX_NAME];
    char name[FACET_IDL_MAX_NAME];
} FacetIdlField;

typedef struct FacetIdlEnumValue {
    char name[FACET_IDL_MAX_NAME];
    int64_t value;
} FacetIdlEnumValue;

typedef struct FacetIdlTypeDecl {
    FacetIdlTypeKind kind;
    char name[FACET_IDL_MAX_NAME];
    char underlying_type[FACET_IDL_MAX_NAME];
    size_t field_count;
    FacetIdlField fields[FACET_IDL_MAX_FIELDS];
    size_t enum_value_count;
    FacetIdlEnumValue enum_values[FACET_IDL_MAX_ENUM_VALUES];
} FacetIdlTypeDecl;

typedef struct FacetIdlInterface {
    char name[FACET_IDL_MAX_NAME];
    char uuid[64];
    int uuid_auto;
    size_t required_count;
    char required[FACET_IDL_MAX_REQUIRED][FACET_IDL_MAX_NAME];
    size_t method_count;
    FacetIdlMethod methods[FACET_IDL_MAX_METHODS];
    size_t property_count;
    FacetIdlProperty properties[FACET_IDL_MAX_METHODS];
    size_t type_count;
    FacetIdlTypeDecl types[FACET_IDL_MAX_TYPES];
} FacetIdlInterface;

typedef struct FacetIdlContext {
    FacetIdlInterface interface;
    /* Number of leading methods imported from IGenericObject. */
    size_t generic_method_count;
    int errors;
    char error[256];
} FacetIdlContext;

void facet_idl_context_init(FacetIdlContext *context);
int facet_idl_add_required(FacetIdlContext *context, const char *name);
FacetIdlMethod *facet_idl_add_method(
    FacetIdlContext *context,
    const char *return_type,
    const char *name
);
int facet_idl_add_param(
    FacetIdlMethod *method,
    FacetIdlDirection direction,
    const char *type,
    const char *name
);
int facet_idl_add_property(
    FacetIdlContext *context,
    const char *type,
    const char *name,
    int readable,
    int writable
);
FacetIdlTypeDecl *facet_idl_add_type(
    FacetIdlContext *context,
    FacetIdlTypeKind kind,
    const char *name,
    const char *underlying_type
);
int facet_idl_add_field(
    FacetIdlTypeDecl *type,
    const char *field_type,
    const char *name
);
int facet_idl_add_enum_value(
    FacetIdlTypeDecl *type,
    const char *name,
    int64_t value
);
