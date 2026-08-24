#include "generator.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *c_type(const char *type)
{
    if (strcmp(type, "u8") == 0) return "uint8_t";
    if (strcmp(type, "u16") == 0) return "uint16_t";
    if (strcmp(type, "u32") == 0) return "uint32_t";
    if (strcmp(type, "u64") == 0) return "uint64_t";
    if (strcmp(type, "i8") == 0) return "int8_t";
    if (strcmp(type, "i16") == 0) return "int16_t";
    if (strcmp(type, "i32") == 0) return "int32_t";
    if (strcmp(type, "i64") == 0) return "int64_t";
    if (strcmp(type, "bool") == 0) return "bool";
    if (strcmp(type, "uuid") == 0) return "uuid_t";
    if (strcmp(type, "handle") == 0) return "FacetHandle";
    if (strcmp(type, "string") == 0) return "FacetString";
    if (strcmp(type, "local_ptr") == 0) return "void *";
    if (strncmp(type, "array<", 6) == 0) {
        static char array_type[FACET_IDL_MAX_NAME * 2];
        snprintf(array_type, sizeof(array_type), "FacetArray_%s", type + 6);
        char *end = strrchr(array_type, '>');
        if (end != NULL) *end = '\0';
        return array_type;
    }
    return type;
}

static int is_array_type(const char *type)
{
    return strncmp(type, "array<", 6) == 0 && strchr(type + 6, '>') != NULL;
}

static const char *array_element_type(const char *type)
{
    return type + 6;
}

static void emit_array_type(FILE *file, const char *type,
                            char seen[][FACET_IDL_MAX_NAME * 2], size_t *seen_count)
{
    char array_name[FACET_IDL_MAX_NAME * 2];
    char element_name[FACET_IDL_MAX_NAME * 2];
    snprintf(array_name, sizeof(array_name), "FacetArray_%s", type + 6);
    char *end = strrchr(array_name, '>');
    if (end != NULL) *end = '\0';
    for (size_t i = 0; i < *seen_count; i++) {
        if (strcmp(seen[i], array_name) == 0) return;
    }
    if (*seen_count >= FACET_IDL_MAX_TYPES) return;
    snprintf(seen[(*seen_count)++], FACET_IDL_MAX_NAME * 2, "%s", array_name);
    snprintf(element_name, sizeof(element_name), "%s",
             array_element_type(type));
    end = strrchr(element_name, '>');
    if (end != NULL) *end = '\0';
    if (is_array_type(element_name))
        emit_array_type(file, element_name, seen, seen_count);
    fprintf(file, "typedef struct %s {\n    %s *data;\n    size_t count;\n"
            "} %s;\n\n", array_name, c_type(element_name), array_name);
}

static const char *meta_type(const char *type)
{
    if (strcmp(type, "u8") == 0) return "FACET_TYPE_U8";
    if (strcmp(type, "u16") == 0) return "FACET_TYPE_U16";
    if (strcmp(type, "u32") == 0) return "FACET_TYPE_U32";
    if (strcmp(type, "u64") == 0) return "FACET_TYPE_U64";
    if (strcmp(type, "i8") == 0) return "FACET_TYPE_I8";
    if (strcmp(type, "i16") == 0) return "FACET_TYPE_I16";
    if (strcmp(type, "i32") == 0) return "FACET_TYPE_I32";
    if (strcmp(type, "i64") == 0) return "FACET_TYPE_I64";
    if (strcmp(type, "bool") == 0) return "FACET_TYPE_BOOL";
    if (strcmp(type, "uuid") == 0) return "FACET_TYPE_UUID";
    if (strcmp(type, "handle") == 0) return "FACET_TYPE_HANDLE";
    if (strcmp(type, "string") == 0) return "FACET_TYPE_STRING";
    if (strcmp(type, "bytes") == 0) return "FACET_TYPE_BYTES";
    if (strcmp(type, "local_ptr") == 0) return "FACET_TYPE_LOCAL_PTR";
    if (is_array_type(type)) return "FACET_TYPE_ARRAY";
    return "FACET_TYPE_STRUCT";
}

static const char *type_meta_symbol(const char *type)
{
    static char symbol[FACET_IDL_MAX_NAME * 2];
    if (strcmp(type, "u8") == 0 || strcmp(type, "u16") == 0 ||
        strcmp(type, "u32") == 0 || strcmp(type, "u64") == 0 ||
        strcmp(type, "i8") == 0 || strcmp(type, "i16") == 0 ||
        strcmp(type, "i32") == 0 || strcmp(type, "i64") == 0 ||
        strcmp(type, "bool") == 0 || strcmp(type, "uuid") == 0 ||
        strcmp(type, "handle") == 0 || strcmp(type, "string") == 0 ||
        strcmp(type, "local_ptr") == 0 || strcmp(type, "bytes") == 0) {
        return NULL;
    }
    snprintf(symbol, sizeof(symbol), "&%s_TypeMeta",
             is_array_type(type) ? c_type(type) : type);
    return symbol;
}

static const char *type_kind(const char *type)
{
    if (is_array_type(type)) return "FACET_TYPE_ARRAY";
    if (strcmp(type, "u8") == 0) return "FACET_TYPE_U8";
    if (strcmp(type, "u16") == 0) return "FACET_TYPE_U16";
    if (strcmp(type, "u32") == 0) return "FACET_TYPE_U32";
    if (strcmp(type, "u64") == 0) return "FACET_TYPE_U64";
    if (strcmp(type, "i8") == 0) return "FACET_TYPE_I8";
    if (strcmp(type, "i16") == 0) return "FACET_TYPE_I16";
    if (strcmp(type, "i32") == 0) return "FACET_TYPE_I32";
    if (strcmp(type, "i64") == 0) return "FACET_TYPE_I64";
    if (strcmp(type, "bool") == 0) return "FACET_TYPE_BOOL";
    if (strcmp(type, "uuid") == 0) return "FACET_TYPE_UUID";
    if (strcmp(type, "handle") == 0) return "FACET_TYPE_HANDLE";
    if (strcmp(type, "string") == 0) return "FACET_TYPE_STRING";
    if (strcmp(type, "local_ptr") == 0) return "FACET_TYPE_LOCAL_PTR";
    return "FACET_TYPE_STRUCT";
}

static const char *type_kind_for(const FacetIdlInterface *definition,
                                 const char *type)
{
    for (size_t i = 0; i < definition->type_count; i++) {
        if (strcmp(definition->types[i].name, type) == 0) {
            return definition->types[i].kind == FACET_IDL_TYPE_ENUM
                ? "FACET_TYPE_ENUM" : "FACET_TYPE_STRUCT";
        }
    }
    return type_kind(type);
}

static const char *meta_type_for(const FacetIdlInterface *definition,
                                 const char *type)
{
    if (is_array_type(type)) return "FACET_TYPE_ARRAY";
    for (size_t i = 0; i < definition->type_count; i++) {
        if (strcmp(definition->types[i].name, type) == 0) {
            return definition->types[i].kind == FACET_IDL_TYPE_ENUM
                ? "FACET_TYPE_ENUM" : "FACET_TYPE_STRUCT";
        }
    }
    return meta_type(type);
}

static void emit_array_metadata_forward(FILE *file, const char *type,
                                        char seen[][FACET_IDL_MAX_NAME * 2],
                                        size_t *seen_count)
{
    if (!is_array_type(type)) return;
    char name[FACET_IDL_MAX_NAME * 2];
    snprintf(name, sizeof(name), "%s", c_type(type));
    for (size_t i = 0; i < *seen_count; i++)
        if (strcmp(seen[i], name) == 0) return;
    if (*seen_count >= FACET_IDL_MAX_TYPES) return;
    snprintf(seen[(*seen_count)++], FACET_IDL_MAX_NAME * 2, "%s", name);
    fprintf(file, "static const FacetTypeMeta %s_TypeMeta;\n", name);
    char element[FACET_IDL_MAX_NAME * 2];
    snprintf(element, sizeof(element), "%s", array_element_type(type));
    char *end = strrchr(element, '>');
    if (end != NULL) *end = '\0';
    emit_array_metadata_forward(file, element, seen, seen_count);
}

static void emit_type_metadata(FILE *file,
                               const FacetIdlInterface *definition)
{
    char forward_arrays[FACET_IDL_MAX_TYPES][FACET_IDL_MAX_NAME * 2];
    size_t forward_array_count = 0;
    for (size_t i = 0; i < definition->type_count; i++)
        for (size_t j = 0; j < definition->types[i].field_count; j++)
            emit_array_metadata_forward(file, definition->types[i].fields[j].type,
                                        forward_arrays, &forward_array_count);
    for (size_t i = 0; i < definition->property_count; i++)
        emit_array_metadata_forward(file, definition->properties[i].type,
                                    forward_arrays, &forward_array_count);
    for (size_t i = 0; i < definition->method_count; i++)
        for (size_t j = 0; j < definition->methods[i].parameter_count; j++)
            emit_array_metadata_forward(file, definition->methods[i].parameters[j].type,
                                        forward_arrays, &forward_array_count);
    if (forward_array_count != 0) fprintf(file, "\n");
    char seen_arrays[FACET_IDL_MAX_TYPES][FACET_IDL_MAX_NAME * 2];
    size_t seen_array_count = 0;
    #define EMIT_ARRAY_META(array_type) do { \
        const char *array = (array_type); \
        if (!is_array_type(array)) break; \
        char array_name[FACET_IDL_MAX_NAME * 2]; \
        snprintf(array_name, sizeof(array_name), "%s", c_type(array)); \
        int exists = 0; \
        for (size_t k = 0; k < seen_array_count; k++) \
            if (strcmp(seen_arrays[k], array_name) == 0) exists = 1; \
        if (exists || seen_array_count >= FACET_IDL_MAX_TYPES) break; \
        snprintf(seen_arrays[seen_array_count++], sizeof(seen_arrays[0]), "%s", array_name); \
        char element_name[FACET_IDL_MAX_NAME * 2]; \
        snprintf(element_name, sizeof(element_name), "%s", array_element_type(array)); \
        char *array_end = strrchr(element_name, '>'); \
        if (array_end != NULL) *array_end = '\0'; \
        const char *element_symbol = type_meta_symbol(element_name); \
        fprintf(file, "static const FacetTypeMeta %s_TypeMeta = {\n" \
                "    .kind = FACET_TYPE_ARRAY, .name = \"%s\",\n" \
                "    .size = sizeof(%s), .element_kind = %s,\n" \
                "    .element_type = %s,\n};\n\n", array_name, array_name, \
                    c_type(element_name), type_kind_for(definition, element_name), \
                element_symbol == NULL ? "NULL" : element_symbol); \
    } while (0)
    for (size_t i = 0; i < definition->type_count; i++) {
        const FacetIdlTypeDecl *type = &definition->types[i];
        if (type->kind == FACET_IDL_TYPE_ENUM) {
            fprintf(file, "static const FacetEnumValueMeta %s_EnumValues[] = {\n",
                    type->name);
            for (size_t j = 0; j < type->enum_value_count; j++) {
                fprintf(file, "    { \"%s\", %lld },\n",
                        type->enum_values[j].name,
                        (long long)type->enum_values[j].value);
            }
            fprintf(file, "};\n");
            fprintf(file, "static const FacetTypeMeta %s_TypeMeta = {\n"
                    "    .kind = FACET_TYPE_ENUM,\n"
                    "    .underlying_kind = %s,\n"
                    "    .name = \"%s\", .size = sizeof(%s),\n"
                    "    .enum_values = %s_EnumValues,\n"
                    "    .enum_value_count = %zu,\n};\n\n",
                    type->name, type_kind(type->underlying_type), type->name,
                    type->name, type->name, type->enum_value_count);
        } else {
            fprintf(file, "static const FacetStructFieldMeta %s_StructFields[] = {\n",
                    type->name);
            for (size_t j = 0; j < type->field_count; j++) {
                const FacetIdlField *field = &type->fields[j];
                const char *symbol = type_meta_symbol(field->type);
                fprintf(file, "    { \"%s\", %s, %s, offsetof(%s, %s) },\n",
                        field->name, type_kind_for(definition, field->type),
                        symbol == NULL ? "NULL" : symbol,
                        type->name, field->name);
            }
            fprintf(file, "};\n");
            fprintf(file, "static const FacetTypeMeta %s_TypeMeta = {\n"
                    "    .kind = FACET_TYPE_STRUCT, .name = \"%s\",\n"
                    "    .size = sizeof(%s), .struct_fields = %s_StructFields,\n"
                    "    .struct_field_count = %zu,\n};\n\n",
                    type->name, type->name, type->name, type->name,
                    type->field_count);
        }
    }
    for (size_t i = 0; i < definition->type_count; i++) {
        const FacetIdlTypeDecl *type = &definition->types[i];
        for (size_t j = 0; j < type->field_count; j++) {
            EMIT_ARRAY_META(type->fields[j].type);
        }
    }
    for (size_t i = 0; i < definition->property_count; i++)
        EMIT_ARRAY_META(definition->properties[i].type);
    for (size_t i = 0; i < definition->method_count; i++)
        for (size_t j = 0; j < definition->methods[i].parameter_count; j++)
            EMIT_ARRAY_META(definition->methods[i].parameters[j].type);
    #undef EMIT_ARRAY_META
}

static int word_type(const char *type)
{
    return strcmp(type, "u8") == 0 || strcmp(type, "u16") == 0 ||
           strcmp(type, "u32") == 0 || strcmp(type, "u64") == 0 ||
           strcmp(type, "i8") == 0 || strcmp(type, "i16") == 0 ||
           strcmp(type, "i32") == 0 || strcmp(type, "i64") == 0 ||
           strcmp(type, "bool") == 0 || strcmp(type, "local_ptr") == 0;
}

static int server_supported_param(const FacetIdlParam *parameter)
{
    if (strcmp(parameter->type, "handle") == 0) {
        return 1;
    }
    if (strcmp(parameter->type, "uuid") == 0) {
        return parameter->direction != FACET_IDL_OUT;
    }
    return 1;
}

static int payload_type(const char *type)
{
    return strcmp(type, "string") == 0 || is_array_type(type) ||
           (!word_type(type) && strcmp(type, "uuid") != 0 &&
            strcmp(type, "handle") != 0 && strcmp(type, "local_ptr") != 0);
}

static int payload_type_for(const FacetIdlInterface *definition,
                            const char *type)
{
    for (size_t i = 0; i < definition->type_count; i++) {
        if (strcmp(definition->types[i].name, type) == 0)
            return definition->types[i].kind == FACET_IDL_TYPE_STRUCT;
    }
    return payload_type(type);
}

static void emit_param_decl(FILE *file, const FacetIdlInterface *definition,
                            const FacetIdlParam *parameter)
{
    const char *type = c_type(parameter->type);
    int payload = payload_type_for(definition, parameter->type);
    if (parameter->direction == FACET_IDL_OUT) {
        if (payload) {
            fprintf(file, "%s *%s", type, parameter->name);
        } else if (strcmp(parameter->type, "handle") == 0) {
            fprintf(file, "FacetHandle *%s", parameter->name);
        } else {
            fprintf(file, "%s *%s", type, parameter->name);
        }
    } else if (parameter->direction == FACET_IDL_INOUT) {
        fprintf(file, "%s *%s", type, parameter->name);
    } else {
        if (payload) {
            fprintf(file, "const %s *%s", type, parameter->name);
        } else {
            fprintf(file, "%s %s", type, parameter->name);
        }
    }
}

static void emit_type_declarations(FILE *file,
                                   const FacetIdlInterface *definition)
{
    char seen_arrays[FACET_IDL_MAX_TYPES][FACET_IDL_MAX_NAME * 2];
    size_t seen_array_count = 0;
    for (size_t i = 0; i < definition->type_count; i++) {
        const FacetIdlTypeDecl *type = &definition->types[i];
        if (type->kind == FACET_IDL_TYPE_ENUM) {
            fprintf(file, "typedef enum %s {\n", type->name);
            for (size_t j = 0; j < type->enum_value_count; j++) {
                fprintf(file, "    %s_%s = %lld%s\n", type->name,
                        type->enum_values[j].name,
                        (long long)type->enum_values[j].value,
                        j + 1 == type->enum_value_count ? "" : ",");
            }
            fprintf(file, "} %s;\n\n", type->name);
        }
    }
    for (size_t i = 0; i < definition->type_count; i++) {
        if (definition->types[i].kind == FACET_IDL_TYPE_STRUCT)
            fprintf(file, "typedef struct %s %s;\n",
                    definition->types[i].name, definition->types[i].name);
    }
    if (definition->type_count != 0) fprintf(file, "\n");
    for (size_t i = 0; i < definition->type_count; i++) {
        const FacetIdlTypeDecl *type = &definition->types[i];
        for (size_t j = 0; j < type->field_count; j++) {
            if (is_array_type(type->fields[j].type)) {
                emit_array_type(file, type->fields[j].type,
                                seen_arrays, &seen_array_count);
            }
        }
    }
    for (size_t i = 0; i < definition->property_count; i++) {
        const char *type = definition->properties[i].type;
        if (is_array_type(type)) {
            emit_array_type(file, type, seen_arrays, &seen_array_count);
        }
    }
    for (size_t i = 0; i < definition->method_count; i++) {
        for (size_t j = 0; j < definition->methods[i].parameter_count; j++) {
            const char *type = definition->methods[i].parameters[j].type;
            if (is_array_type(type)) {
                emit_array_type(file, type, seen_arrays, &seen_array_count);
            }
        }
    }
    for (size_t i = 0; i < definition->type_count; i++) {
        const FacetIdlTypeDecl *type = &definition->types[i];
        if (type->kind != FACET_IDL_TYPE_STRUCT) continue;
        fprintf(file, "struct %s {\n", type->name);
        for (size_t j = 0; j < type->field_count; j++) {
            fprintf(file, "    %s %s;\n",
                    c_type(type->fields[j].type), type->fields[j].name);
        }
        fprintf(file, "};\n\n");
    }
}

static void emit_method_signature(FILE *file, const FacetIdlInterface *definition,
                                  const FacetIdlMethod *method)
{
    fprintf(file, "    %s (*%s)(void *self", c_type(method->return_type),
            method->name);
    for (size_t i = 0; i < method->parameter_count; i++) {
        fprintf(file, ", ");
        emit_param_decl(file, definition, &method->parameters[i]);
    }
    fprintf(file, ");\n");
}

static void emit_proxy_method(FILE *file, const char *interface,
                              const FacetIdlInterface *definition,
                              const FacetIdlMethod *method, size_t index)
{
    fprintf(file, "static inline FacetResult %s_proxy_%s(void *self",
            interface, method->name);
    for (size_t i = 0; i < method->parameter_count; i++) {
        fprintf(file, ", ");
        emit_param_decl(file, definition, &method->parameters[i]);
    }
    fprintf(file, ")\n{\n    return libfacet_proxy_client_call(\n");
    fprintf(file, "        self, &%s_Methods[%zu]", interface, index);
    for (size_t i = 0; i < method->parameter_count; i++) {
        fprintf(file, ", %s", method->parameters[i].name);
    }
    fprintf(file, ");\n}\n\n");
}

static int emit_uuid(FILE *file, const char *interface, const char *uuid,
                     int uuid_auto)
{
    if (uuid_auto) {
        unsigned char bytes[16];
        int descriptor = open("/dev/urandom", O_RDONLY);
        if (descriptor < 0 || read(descriptor, bytes, sizeof(bytes)) !=
            (ssize_t)sizeof(bytes)) {
            if (descriptor >= 0) close(descriptor);
            return -1;
        }
        close(descriptor);
        unsigned long long a = ((unsigned long long)bytes[0] << 24) |
            ((unsigned long long)bytes[1] << 16) |
            ((unsigned long long)bytes[2] << 8) | bytes[3];
        unsigned long long b = ((unsigned long long)bytes[4] << 8) | bytes[5];
        unsigned long long c = ((unsigned long long)bytes[6] << 8) | bytes[7];
        unsigned long long d = ((unsigned long long)bytes[8] << 8) | bytes[9];
        unsigned long long e = ((unsigned long long)bytes[10] << 40) |
            ((unsigned long long)bytes[11] << 32) |
            ((unsigned long long)bytes[12] << 24) |
            ((unsigned long long)bytes[13] << 16) |
            ((unsigned long long)bytes[14] << 8) | bytes[15];
        fprintf(file,
                "static const uuid_t IID_%s = UUID_INIT(0x%08llx,0x%04llx,"
                "0x%04llx,0x%04llx,0x%012llxULL);\n",
                interface, a, b, c, d, e);
        return 0;
    }
    unsigned int nibbles[32];
    size_t count = 0;
    for (const char *cursor = uuid; *cursor != '\0'; cursor++) {
        if (*cursor == '-') continue;
        if (!isxdigit((unsigned char)*cursor) || count >= 32) {
            fprintf(file, "static const uuid_t IID_%s = UUID_INIT(0, 0, 0, 0, 0ULL);\n",
                    interface);
            return 0;
        }
        unsigned int value = (unsigned int)(isdigit((unsigned char)*cursor)
            ? *cursor - '0'
            : tolower((unsigned char)*cursor) - 'a' + 10);
        nibbles[count++] = value;
    }
    if (count != 32) {
        fprintf(file, "static const uuid_t IID_%s = UUID_INIT(0, 0, 0, 0, 0ULL);\n",
                interface);
        return 0;
    }

    unsigned long long a = 0;
    unsigned long long b = 0;
    unsigned long long c = 0;
    unsigned long long d = 0;
    unsigned long long e = 0;
    for (size_t i = 0; i < 8; i++) a = (a << 4) | nibbles[i];
    for (size_t i = 8; i < 12; i++) b = (b << 4) | nibbles[i];
    for (size_t i = 12; i < 16; i++) c = (c << 4) | nibbles[i];
    for (size_t i = 16; i < 20; i++) d = (d << 4) | nibbles[i];
    for (size_t i = 20; i < 32; i++) e = (e << 4) | nibbles[i];

    fprintf(file,
            "static const uuid_t IID_%s = UUID_INIT(0x%08llx,0x%04llx,"
            "0x%04llx,0x%04llx,0x%012llxULL);\n",
            interface, a, b, c, d, e);
    return 0;
}

static void emit_server_method(FILE *file,
                               const FacetIdlInterface *definition,
                               const FacetIdlMethod *method)
{
    const char *interface = definition->name;
    fprintf(file, "static inline FacetResult %s_server_%s(\n"
            "    void *interface_object,\n"
            "    const FacetRpcMessage *request,\n"
            "    FacetRpcMessage *reply)\n{\n",
            interface, method->name);
    size_t input_words = 0;
    int request_uses_payload = 0;
    int reply_uses_payload = 0;
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetIdlParam *parameter = &method->parameters[i];
        if (!server_supported_param(parameter)) {
            fprintf(file, "    (void)interface_object; (void)request; (void)reply;\n"
                    "    return FACET_NOT_SUPPORTED;\n}\n\n");
            return;
        }
        if (payload_type_for(definition, parameter->type)) {
            if (parameter->direction != FACET_IDL_OUT)
                request_uses_payload = 1;
            if (parameter->direction != FACET_IDL_IN)
                reply_uses_payload = 1;
        }
        if (parameter->direction != FACET_IDL_OUT) {
            if (strcmp(parameter->type, "handle") != 0 &&
                strcmp(parameter->type, "uuid") != 0 &&
                !payload_type_for(definition, parameter->type)) {
                input_words++;
            } else if (strcmp(parameter->type, "uuid") == 0) {
                input_words += 2;
            }
        }
    }
    fprintf(file, "    if (request->word_count != %zu) return FACET_PROTOCOL_ERROR;\n",
            input_words);
    if (request_uses_payload) {
        fprintf(file, "    FacetRpcCodec request_codec = { .data = request->payload, "
                ".size = request->payload_size, .capacity = request->payload_size };\n");
    }
    size_t word_index = 0;
    size_t handle_index = 0;
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetIdlParam *parameter = &method->parameters[i];
        if (parameter->direction != FACET_IDL_OUT &&
            strcmp(parameter->type, "handle") == 0) {
            size_t current_handle = handle_index++;
            fprintf(file, "    FacetHandle %s = {0};\n"
                    "    if (request->attachment_count <= %zu || request->attachments[%zu].kind != FACET_RPC_ATTACHMENT_HANDLE) return FACET_PROTOCOL_ERROR;\n"
                    "    %s = request->attachments[%zu].handle;\n",
                    parameter->name, current_handle, current_handle,
                    parameter->name, current_handle);
        } else if (parameter->direction != FACET_IDL_OUT &&
            strcmp(parameter->type, "uuid") == 0) {
            fprintf(file, "    uuid_t %s;\n"
                    "    memcpy(&%s, &request->words[%zu], sizeof(%s));\n",
                    parameter->name, parameter->name, word_index, parameter->name);
            word_index += 2;
        } else if (parameter->direction != FACET_IDL_OUT &&
                   payload_type_for(definition, parameter->type)) {
            const char *meta = type_meta_symbol(parameter->type);
            fprintf(file, "    %s %s = {0};\n"
                    "    if (facet_rpc_decode_value(&request_codec, %s, %s, &%s) != FACET_OK) return FACET_PROTOCOL_ERROR;\n",
                    c_type(parameter->type), parameter->name,
                    meta_type_for(definition, parameter->type),
                    meta == NULL ? "NULL" : meta, parameter->name);
        } else if (parameter->direction != FACET_IDL_OUT) {
            fprintf(file, "    %s %s = (%s)request->words[%zu];\n",
                    c_type(parameter->type), parameter->name,
                    c_type(parameter->type), word_index++);
        } else if (strcmp(parameter->type, "handle") == 0) {
            fprintf(file, "    FacetHandle %s = {0};\n", parameter->name);
        } else {
            fprintf(file, "    %s %s = {0};\n", c_type(parameter->type),
                    parameter->name);
        }
    }
    fprintf(file, "    FacetResult call_result = ((%s *)interface_object)->%s(\n"
            "        ((%s *)interface_object)->self",
            interface, method->name, interface);
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetIdlParam *parameter = &method->parameters[i];
        const char *prefix = parameter->direction == FACET_IDL_IN
            ? (payload_type_for(definition, parameter->type) ? "&" : "") : "&";
        fprintf(file, ", %s%s", prefix, parameter->name);
    }
    fprintf(file, ");\n    reply->word_count = 0;\n"
            "    reply->words[reply->word_count++] = (uint64_t)(int64_t)call_result;\n");
    if (reply_uses_payload) {
        fprintf(file, "    FacetRpcCodec reply_codec = {0};\n");
    }
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetIdlParam *parameter = &method->parameters[i];
        if (parameter->direction == FACET_IDL_IN) continue;
        if (payload_type_for(definition, parameter->type)) {
            const char *meta = type_meta_symbol(parameter->type);
            fprintf(file, "    if (reply_codec.data == NULL && facet_rpc_codec_init(&reply_codec, 256) != FACET_OK) return FACET_OUT_OF_MEMORY;\n"
                    "    if (facet_rpc_encode_value(&reply_codec, %s, %s, &%s) != FACET_OK) return FACET_PROTOCOL_ERROR;\n"
                    "    reply->payload = reply_codec.data; reply->payload_size = reply_codec.size;\n",
                    meta_type_for(definition, parameter->type),
                    meta == NULL ? "NULL" : meta, parameter->name);
        } else if (strcmp(parameter->type, "handle") == 0) {
            fprintf(file, "    if (reply->attachment_count >= FACET_RPC_MAX_ATTACHMENTS) return FACET_BUFFER_TOO_SMALL;\n"
                    "    reply->attachments[reply->attachment_count].kind = FACET_RPC_ATTACHMENT_HANDLE;\n"
                    "    reply->attachments[reply->attachment_count++].handle = %s;\n", parameter->name);
        } else {
            fprintf(file, "    reply->words[reply->word_count++] = (uint64_t)%s;\n",
                    parameter->name);
        }
    }
    fprintf(file, "    return FACET_OK;\n}\n\n");
}

static void emit_proxy_initializer(FILE *file, const char *interface,
                                   const FacetIdlInterface *definition)
{
    fprintf(file, "static inline void %s_initialize_proxy(\n", interface);
    fprintf(file, "    void *interface_object, void *state)\n{\n");
    fprintf(file, "    %s *object = interface_object;\n", interface);
    fprintf(file, "    object->self = object;\n");
    fprintf(file, "    object->priv = state;\n");
    for (size_t i = 0; i < definition->method_count; i++) {
        fprintf(file, "    object->%s = %s_proxy_%s;\n",
                definition->methods[i].name, interface,
                definition->methods[i].name);
    }
    fprintf(file, "}\n\n");
}

int facet_idl_write_header(
    const FacetIdlContext *context,
    const char *output_path)
{
    const FacetIdlInterface *definition = &context->interface;
    FILE *file = fopen(output_path, "w");
    if (file == NULL) return -1;

    fprintf(file, "#pragma once\n\n");
    fprintf(file, "#include <stdbool.h>\n#include <stdint.h>\n");
    fprintf(file, "#include <stddef.h>\n");
    fprintf(file, "#include <string.h>\n");
    fprintf(file, "#include <facetos/libfacet/common.h>\n\n");
    for (size_t i = 0; i < definition->required_count; i++) {
        if (strcmp(definition->required[i], definition->name) != 0) {
            fprintf(file, "#include <facetos/interfaces/%s.h>\n",
                    definition->required[i]);
        }
    }
    if (definition->required_count != 0) fprintf(file, "\n");
    emit_type_declarations(file, definition);
    emit_type_metadata(file, definition);
    if (emit_uuid(file, definition->name, definition->uuid,
                  definition->uuid_auto) != 0) {
        fclose(file);
        return -1;
    }
    fprintf(file, "static const char %s_InterfaceName[] = \"%s\";\n\n",
            definition->name, definition->name);

    fprintf(file, "enum {\n");
    for (size_t i = 0; i < definition->method_count; i++) {
        fprintf(file, "    %s_METHOD_%s = %u%s\n", definition->name,
                definition->methods[i].name, definition->methods[i].id,
                i + 1 == definition->method_count ? "" : ",");
    }
    fprintf(file, "};\n\n");

    if (definition->required_count == 0) {
        fprintf(file, "static const size_t %s_RequiredInterfacesCount = 0;\n",
                definition->name);
    } else {
        fprintf(file, "static const uuid_t %s_RequiredInterfaces[] = {\n",
                definition->name);
        for (size_t i = 0; i < definition->required_count; i++) {
            fprintf(file, "    IID_%s,\n", definition->required[i]);
        }
        fprintf(file, "};\nstatic const size_t %s_RequiredInterfacesCount = "
                "sizeof(%s_RequiredInterfaces) / sizeof(%s_RequiredInterfaces[0]);\n",
                definition->name, definition->name, definition->name);
    }

    fprintf(file, "\ntypedef struct %s {\n", definition->name);
    fprintf(file, "    void *self;\n    void *priv;\n");
    for (size_t i = 0; i < definition->property_count; i++) {
        fprintf(file, "    %s _%s;\n", c_type(definition->properties[i].type),
                definition->properties[i].name);
    }
    for (size_t i = 0; i < definition->method_count; i++) {
        emit_method_signature(file, definition, &definition->methods[i]);
    }
    fprintf(file, "} %s;\n\n", definition->name);

    for (size_t i = 0; i < definition->method_count; i++) {
        const FacetIdlMethod *method = &definition->methods[i];
        if (method->parameter_count == 0) {
            fprintf(file, "static const FacetParamMeta %s_%s_Params[] = {0};\n",
                    definition->name, method->name);
        } else {
            fprintf(file, "static const FacetParamMeta %s_%s_Params[] = {\n",
                    definition->name, method->name);
            for (size_t j = 0; j < method->parameter_count; j++) {
                const FacetIdlParam *parameter = &method->parameters[j];
                const char *type = meta_type_for(definition, parameter->type);
                const char *direction = parameter->direction == FACET_IDL_IN
                    ? "FACET_PARAM_IN"
                    : parameter->direction == FACET_IDL_OUT
                        ? "FACET_PARAM_OUT" : "FACET_PARAM_INOUT";
                const char *metadata = type_meta_symbol(parameter->type);
                fprintf(file, "    { \"%s\", %s, %s, 0, -1, %s },\n",
                        parameter->name, type ? type : "FACET_TYPE_BYTES",
                        direction, metadata == NULL ? "NULL" : metadata);
            }
            fprintf(file, "};\n");
        }
    }

    fprintf(file, "static const FacetMethodMeta %s_Methods[];\n\n",
            definition->name);
    for (size_t i = 0; i < definition->method_count; i++) {
        fprintf(file, "static inline FacetResult %s_server_%s(void *, const FacetRpcMessage *, FacetRpcMessage *);\n",
                definition->name, definition->methods[i].name);
    }
    fprintf(file, "static const FacetMethodMeta %s_Methods[] = {\n",
            definition->name);
    for (size_t i = 0; i < definition->method_count; i++) {
        const FacetIdlMethod *method = &definition->methods[i];
        fprintf(file, "    { %u, \"%s\", offsetof(%s, %s), %zu, %s_%s_Params, "
                "%s_server_%s },\n", method->id, method->name, definition->name,
                method->name, method->parameter_count, definition->name,
                method->name, definition->name, method->name);
    }
    fprintf(file, "};\n\n");

    for (size_t i = 0; i < definition->method_count; i++) {
        emit_proxy_method(file, definition->name, definition,
                          &definition->methods[i], i);
    }

    emit_proxy_initializer(file, definition->name, definition);
    for (size_t i = 0; i < definition->method_count; i++) {
        emit_server_method(file, definition, &definition->methods[i]);
    }

    fprintf(file, "static const FacetInterfaceMeta %s_MetaData = {\n",
            definition->name);
    fprintf(file, "    .iid = IID_%s,\n", definition->name);
    fprintf(file, "    .name = %s_InterfaceName,\n", definition->name);
    fprintf(file, "    .interface_size = sizeof(%s),\n", definition->name);
    fprintf(file, "    .required_interface_count = %s_RequiredInterfacesCount,\n",
            definition->name);
    fprintf(file, "    .required_interfaces = ");
    if (definition->required_count == 0) fprintf(file, "NULL,\n");
    else fprintf(file, "%s_RequiredInterfaces,\n", definition->name);
    fprintf(file, "    .method_count = sizeof(%s_Methods) / sizeof(%s_Methods[0]),\n",
            definition->name, definition->name);
    fprintf(file, "    .methods = %s_Methods,\n", definition->name);
    fprintf(file, "    .initialize_proxy = %s_initialize_proxy,\n};\n",
            definition->name);
    fclose(file);
    return 0;
}
