#include "ast.h"
#include "generator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern FILE *yyin;
extern int yyparse(void);
extern void yylex_destroy(void);

FacetIdlContext *facet_idl_parser_context;
FacetIdlMethod *facet_idl_current_method;

static int validate(FacetIdlContext *context)
{
    FacetIdlInterface *definition = &context->interface;
    if (definition->name[0] == '\0' || definition->uuid[0] == '\0') {
        fprintf(stderr, "facet-idlc: interface name and uuid are required\n");
        return -1;
    }

    for (size_t i = 0; i < definition->method_count; i++) {
        FacetIdlMethod *method = &definition->methods[i];
        if (method->id < 100) {
            fprintf(stderr, "facet-idlc: method %s has reserved id %u\n",
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
        for (size_t j = 0; j < i; j++) {
            if (definition->methods[j].id == method->id) {
                fprintf(stderr, "facet-idlc: duplicate method id %u\n",
                        method->id);
                return -1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[1], "-o") != 0) {
        fprintf(stderr, "usage: %s -o OUTPUT INPUT\n", argv[0]);
        return 2;
    }

    FILE *input = fopen(argv[3], "r");
    if (input == NULL) {
        perror(argv[3]);
        return 1;
    }

    FacetIdlContext *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        fclose(input);
        fprintf(stderr, "facet-idlc: out of memory\n");
        return 1;
    }
    facet_idl_context_init(context);
    facet_idl_parser_context = context;
    facet_idl_current_method = NULL;
    yyin = input;
    int parse_result = yyparse();
    fclose(input);
    yylex_destroy();

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
