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
    if (strcmp(type, "string") == 0) return "const char *";
    if (strcmp(type, "local_ptr") == 0) return "void *";
    return type;
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
    return NULL;
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
        return parameter->direction == FACET_IDL_OUT;
    }
    if (strcmp(parameter->type, "uuid") == 0) {
        return parameter->direction != FACET_IDL_OUT;
    }
    return word_type(parameter->type);
}

static void emit_param_decl(FILE *file, const FacetIdlParam *parameter)
{
    const char *type = c_type(parameter->type);
    if (parameter->direction == FACET_IDL_OUT) {
        if (strcmp(parameter->type, "string") == 0) {
            fprintf(file, "char **%s", parameter->name);
        } else if (strcmp(parameter->type, "handle") == 0) {
            fprintf(file, "FacetHandle *%s", parameter->name);
        } else {
            fprintf(file, "%s *%s", type, parameter->name);
        }
    } else if (parameter->direction == FACET_IDL_INOUT) {
        fprintf(file, "%s *%s", type, parameter->name);
    } else {
        fprintf(file, "%s %s", type, parameter->name);
    }
}

static void emit_method_signature(FILE *file, const FacetIdlMethod *method)
{
    fprintf(file, "    %s (*%s)(void *self", c_type(method->return_type),
            method->name);
    for (size_t i = 0; i < method->parameter_count; i++) {
        fprintf(file, ", ");
        emit_param_decl(file, &method->parameters[i]);
    }
    fprintf(file, ");\n");
}

static void emit_proxy_method(FILE *file, const char *interface,
                              const FacetIdlMethod *method, size_t index)
{
    fprintf(file, "static inline FacetResult %s_proxy_%s(void *self",
            interface, method->name);
    for (size_t i = 0; i < method->parameter_count; i++) {
        fprintf(file, ", ");
        emit_param_decl(file, &method->parameters[i]);
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

static void emit_server_method(FILE *file, const char *interface,
                               const FacetIdlMethod *method)
{
    fprintf(file, "static inline FacetResult %s_server_%s(\n",
            interface, method->name);
    fprintf(file, "    void *interface_object,\n");
    fprintf(file, "    const FacetRpcMessage *request,\n");
    fprintf(file, "    FacetRpcMessage *reply)\n{\n");
    int supported = 1;
    for (size_t i = 0; i < method->parameter_count; i++) {
        if (!server_supported_param(&method->parameters[i])) supported = 0;
    }
    if (!supported) {
        fprintf(file, "    (void)interface_object; (void)request; (void)reply;\n");
        fprintf(file, "    return FACET_NOT_SUPPORTED;\n}\n\n");
        return;
    }
    size_t input_count = 0;
    for (size_t i = 0; i < method->parameter_count; i++) {
        if (method->parameters[i].direction != FACET_IDL_OUT) {
            input_count += strcmp(method->parameters[i].type, "uuid") == 0 ? 2 : 1;
        }
    }
    fprintf(file, "    if (request->word_count != %zu) return FACET_PROTOCOL_ERROR;\n",
            input_count);
    input_count = 0;
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetIdlParam *parameter = &method->parameters[i];
        if (parameter->direction != FACET_IDL_OUT &&
            strcmp(parameter->type, "uuid") == 0) {
            fprintf(file, "    uuid_t %s;\n"
                    "    memcpy(&%s, &request->words[%zu], sizeof(%s));\n",
                    parameter->name, parameter->name, input_count,
                    parameter->name);
            input_count += 2;
        } else if (parameter->direction != FACET_IDL_OUT) {
            fprintf(file, "    %s %s = (%s)request->words[%zu];\n",
                    c_type(parameter->type), parameter->name,
                    c_type(parameter->type), input_count++);
        } else if (strcmp(parameter->type, "handle") == 0) {
            fprintf(file, "    FacetHandle %s = {0};\n", parameter->name);
        } else {
            fprintf(file, "    %s %s = (%s)0;\n", c_type(parameter->type),
                    parameter->name, c_type(parameter->type));
        }
    }
    fprintf(file, "    FacetResult call_result = ((%s *)interface_object)->%s(\n",
            interface, method->name);
    fprintf(file, "        ((%s *)interface_object)->self", interface);
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetIdlParam *parameter = &method->parameters[i];
        fprintf(file, ", %s%s", parameter->direction == FACET_IDL_IN ? "" : "&",
                parameter->name);
    }
    fprintf(file, ");\n    reply->word_count = 0;\n");
    fprintf(file, "    reply->words[reply->word_count++] = "
            "(uint64_t)(int64_t)call_result;\n");
    for (size_t i = 0; i < method->parameter_count; i++) {
        const FacetIdlParam *parameter = &method->parameters[i];
        if (parameter->direction != FACET_IDL_IN &&
            strcmp(parameter->type, "handle") != 0) {
            fprintf(file, "    reply->words[reply->word_count++] = (uint64_t)%s;\n",
                    parameter->name);
        } else if (parameter->direction != FACET_IDL_IN) {
            fprintf(file, "    if (reply->handle_count >= FACET_RPC_MAX_HANDLES) "
                    "return FACET_BUFFER_TOO_SMALL;\n");
            fprintf(file, "    reply->handles[reply->handle_count++] = %s;\n",
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
        emit_method_signature(file, &definition->methods[i]);
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
                const char *type = meta_type(parameter->type);
                const char *direction = parameter->direction == FACET_IDL_IN
                    ? "FACET_PARAM_IN"
                    : parameter->direction == FACET_IDL_OUT
                        ? "FACET_PARAM_OUT" : "FACET_PARAM_INOUT";
                fprintf(file, "    { \"%s\", %s, %s, 0, -1 },\n",
                        parameter->name, type ? type : "FACET_TYPE_BYTES",
                        direction);
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
        emit_proxy_method(file, definition->name, &definition->methods[i], i);
    }

    emit_proxy_initializer(file, definition->name, definition);
    for (size_t i = 0; i < definition->method_count; i++) {
        emit_server_method(file, definition->name, &definition->methods[i]);
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
