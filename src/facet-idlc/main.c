#include "ast.h"
#include "generator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern FILE *yyin;
extern int yyparse(void);
extern void yylex_destroy(void);

FacetIdlContext *facet_idl_parser_context;
FacetIdlMethod *facet_idl_current_method;
FacetIdlTypeDecl *facet_idl_current_type;

static int validate(FacetIdlContext *context)
{
    FacetIdlInterface *definition = &context->interface;
    if (definition->name[0] == '\0' ||
        (definition->uuid[0] == '\0' && !definition->uuid_auto)) {
        fprintf(stderr, "facet-idlc: interface name and uuid are required\n");
        return -1;
    }

    for (size_t i = 0; i < definition->method_count; i++) {
        FacetIdlMethod *method = &definition->methods[i];
        uint32_t expected_id = i < context->generic_method_count
            ? (uint32_t)i
            : 100u + (uint32_t)(i - context->generic_method_count);
        if (method->id != expected_id) {
            fprintf(stderr, "facet-idlc: method %s has invalid generated id %u\n",
                    method->name, method->id);
            return -1;
        }
        if (strcmp(method->return_type, "FacetResult") != 0) {
            fprintf(stderr, "facet-idlc: method %s must return FacetResult\n",
                    method->name);
            return -1;
        }
        for (size_t j = 0; j < method->parameter_count; j++) {
            FacetIdlParam *parameter = &method->parameters[j];
            if (strcmp(parameter->type, "local_ptr") == 0 &&
                parameter->direction != FACET_IDL_OUT) {
                fprintf(stderr,
                        "facet-idlc: local_ptr %s must be an out parameter\n",
                        parameter->name);
                return -1;
            }
        }
    }
    if (context->generic_method_count > 100) {
        fprintf(stderr, "facet-idlc: IGenericObject has more than 100 methods\n");
        return -1;
    }
    if (context->generic_method_count != 0 &&
        strcmp(definition->methods[0].name, "getInterface") != 0) {
        fprintf(stderr, "facet-idlc: the first IGenericObject method must be getInterface\n");
        return -1;
    }
    return 0;
}

static void assign_method_ids(FacetIdlContext *context, size_t generic_count)
{
    context->generic_method_count = generic_count;
    for (size_t i = 0; i < context->interface.method_count; i++) {
        context->interface.methods[i].id = i < generic_count
            ? (uint32_t)i
            : 100u + (uint32_t)(i - generic_count);
    }
}

static int parse_file(const char *path, FacetIdlContext *context)
{
    FILE *input = fopen(path, "r");
    if (input == NULL) return -1;

    facet_idl_context_init(context);
    facet_idl_parser_context = context;
    facet_idl_current_method = NULL;
    facet_idl_current_type = NULL;
    yyin = input;
    int result = yyparse();
    fclose(input);
    yylex_destroy();

    return result == 0 && context->errors == 0 ? 0 : -1;
}

static int find_generic_idl(const char *input_path, char *path, size_t path_size)
{
    const char *configured = getenv("FACET_IDL_GENERIC_OBJECT");
    if (configured != NULL && access(configured, R_OK) == 0) {
        snprintf(path, path_size, "%s", configured);
        return 0;
    }

    const char *last_slash = strrchr(input_path, '/');
    if (last_slash != NULL) {
        size_t directory_length = (size_t)(last_slash - input_path);
        if (directory_length + sizeof("/IGenericObject.facet") <= path_size) {
            memcpy(path, input_path, directory_length);
            snprintf(path + directory_length,
                     path_size - directory_length,
                     "/IGenericObject.facet");
            if (access(path, R_OK) == 0) return 0;
        }
    }

    static const char *const candidates[] = {
        "idl/IGenericObject.facet",
        "FacetOS/idl/IGenericObject.facet",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (access(candidates[i], R_OK) == 0) {
            snprintf(path, path_size, "%s", candidates[i]);
            return 0;
        }
    }
    return -1;
}

static int is_generic_input(const char *path)
{
    const char *filename = strrchr(path, '/');
    filename = filename == NULL ? path : filename + 1;
    return strcmp(filename, "IGenericObject.facet") == 0;
}

static int import_generic_methods(FacetIdlContext *context,
                                  const FacetIdlContext *generic)
{
    if (generic->interface.method_count > FACET_IDL_MAX_METHODS) return -1;
    if (generic->interface.method_count + context->interface.method_count >
        FACET_IDL_MAX_METHODS) return -1;

    size_t declared_count = context->interface.method_count;
    memmove(&context->interface.methods[generic->interface.method_count],
            &context->interface.methods[0],
            declared_count * sizeof(context->interface.methods[0]));
    memcpy(&context->interface.methods[0], generic->interface.methods,
           generic->interface.method_count * sizeof(context->interface.methods[0]));
    context->interface.method_count += generic->interface.method_count;
    context->generic_method_count = generic->interface.method_count;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[1], "-o") != 0) {
        fprintf(stderr, "usage: %s -o OUTPUT INPUT\n", argv[0]);
        return 2;
    }

    FacetIdlContext *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        fprintf(stderr, "facet-idlc: out of memory\n");
        return 1;
    }
    int parse_result = parse_file(argv[3], context);
    if (parse_result != 0) {
        if (context->error[0] != '\0') fprintf(stderr, "facet-idlc: %s\n", context->error);
        else perror(argv[3]);
        free(context);
        return 1;
    }

    if (!is_generic_input(argv[3])) {
        char generic_path[4096];
        FacetIdlContext generic;
        int generic_result = find_generic_idl(argv[3], generic_path,
                                              sizeof(generic_path));
        if (generic_result == 0) {
            generic_result = parse_file(generic_path, &generic);
        }
        if (generic_result == 0 &&
            strcmp(generic.interface.name, "IGenericObject") != 0) {
            generic_result = -1;
        }
        if (generic_result == 0) {
            assign_method_ids(&generic, generic.interface.method_count);
            generic_result = validate(&generic);
        }
        if (generic_result == 0) {
            generic_result = import_generic_methods(context, &generic);
        }
        if (generic_result == 0) {
            assign_method_ids(context, generic.interface.method_count);
        }
        if (generic_result != 0) {
            fprintf(stderr, "facet-idlc: unable to load IGenericObject.facet\n");
            free(context);
            return 1;
        }
    } else {
        if (strcmp(context->interface.name, "IGenericObject") != 0) {
            fprintf(stderr, "facet-idlc: IGenericObject.facet must define IGenericObject\n");
            free(context);
            return 1;
        }
        assign_method_ids(context, context->interface.method_count);
    }

    if (parse_result != 0 || context->errors != 0 || validate(context) != 0) {
        if (context->error[0] != '\0') {
            fprintf(stderr, "facet-idlc: %s\n", context->error);
        }
        free(context);
        return 1;
    }

    if (facet_idl_write_header(context, argv[2]) != 0) {
        perror(argv[2]);
        free(context);
        return 1;
    }
    free(context);
    return 0;
}
