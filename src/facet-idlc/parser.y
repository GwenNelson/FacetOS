%{
#include "ast.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern int yylex(void);
extern int yylineno;
extern FacetIdlContext *facet_idl_parser_context;
extern FacetIdlMethod *facet_idl_current_method;
extern FacetIdlTypeDecl *facet_idl_current_type;
void yyerror(const char *message);
%}

%union {
    char *string;
    uint32_t number;
}

%token INTERFACE UUID AUTO REQUIRES METHOD PROPERTY ENUM STRUCT ARRAY READ WRITE
%token IN OUT INOUT
%token <string> IDENT STRING
%token <number> NUMBER

%type <string> type_name return_type
%type <number> direction property_access

%%

file:
    interface_decl
;

interface_decl:
    INTERFACE IDENT '{'
    {
        snprintf(facet_idl_parser_context->interface.name,
                 sizeof(facet_idl_parser_context->interface.name),
                 "%s", $2);
        free($2);
    }
    interface_items '}'
;

interface_items:
    /* empty */
  | interface_items interface_item
;

interface_item:
    enum_decl
  | struct_decl
  | UUID STRING ';'
    {
        snprintf(facet_idl_parser_context->interface.uuid,
                 sizeof(facet_idl_parser_context->interface.uuid),
                 "%s", $2);
        free($2);
    }
  | UUID AUTO ';'
    {
        facet_idl_parser_context->interface.uuid_auto = 1;
    }
  | REQUIRES IDENT ';'
    {
        if (facet_idl_add_required(facet_idl_parser_context, $2) != 0) {
            yyerror("too many required interfaces");
        }
        free($2);
    }
  | METHOD return_type IDENT '('
    {
        facet_idl_current_method = facet_idl_add_method(
            facet_idl_parser_context, $2, $3);
        if (facet_idl_current_method == NULL) {
            yyerror("too many methods");
        }
    }
    parameters ')' ';'
    {
        free($2);
        free($3);
        facet_idl_current_method = NULL;
    }
  | PROPERTY type_name IDENT property_access ';'
    {
        if (facet_idl_add_property(
                facet_idl_parser_context, $2, $3,
                ($4 & 1u) != 0, ($4 & 2u) != 0) != 0) {
            yyerror("invalid property");
        }
        free($2);
        free($3);
    }
;

enum_decl:
    ENUM IDENT ':' type_name '{'
    {
        facet_idl_current_type = facet_idl_add_type(
            facet_idl_parser_context, FACET_IDL_TYPE_ENUM, $2, $4);
        if (facet_idl_current_type == NULL) yyerror("too many type declarations");
    }
    enum_values '}' ';'
    {
        free($2);
        free($4);
        facet_idl_current_type = NULL;
    }
;

enum_values:
    /* empty */
  | enum_values enum_value
;

enum_value:
    IDENT '=' NUMBER ';'
    {
        if (facet_idl_add_enum_value(
                facet_idl_current_type, $1, (int64_t)$3) != 0) {
            yyerror("too many enum values");
        }
        free($1);
    }
;

struct_decl:
    STRUCT IDENT '{'
    {
        facet_idl_current_type = facet_idl_add_type(
            facet_idl_parser_context, FACET_IDL_TYPE_STRUCT, $2, NULL);
        if (facet_idl_current_type == NULL) yyerror("too many type declarations");
    }
    struct_fields '}' ';'
    {
        free($2);
        facet_idl_current_type = NULL;
    }
;

struct_fields:
    /* empty */
  | struct_fields struct_field
;

struct_field:
    type_name IDENT ';'
    {
        if (facet_idl_add_field(facet_idl_current_type, $1, $2) != 0) {
            yyerror("too many struct fields");
        }
        free($1);
        free($2);
    }
;

return_type:
    IDENT { $$ = $1; }
;

type_name:
    IDENT { $$ = $1; }
  | UUID
    {
        $$ = strdup("uuid");
    }
  | ARRAY '<' type_name '>'
    {
        size_t length = strlen($3) + 8;
        $$ = malloc(length);
        if ($$ != NULL) snprintf($$, length, "array<%s>", $3);
        free($3);
    }
;

property_access:
    READ
    {
        $$ = 1u;
    }
  | WRITE
    { $$ = 2u; }
  | READ WRITE
    { $$ = 3u; }
  | WRITE READ
    { $$ = 3u; }
;

parameters:
    /* empty */
  | parameter_list
;

parameter_list:
    parameter
  | parameter_list ',' parameter
;

parameter:
    direction type_name IDENT
    {
        if (facet_idl_add_param(
                facet_idl_current_method, $1, $2, $3) != 0) {
            yyerror("too many parameters");
        }
        free($2);
        free($3);
    }
;

direction:
    IN { $$ = FACET_IDL_IN; }
  | OUT { $$ = FACET_IDL_OUT; }
  | INOUT { $$ = FACET_IDL_INOUT; }
;

%%

void yyerror(const char *message)
{
    if (facet_idl_parser_context != NULL) {
        facet_idl_parser_context->errors++;
        snprintf(facet_idl_parser_context->error,
                 sizeof(facet_idl_parser_context->error),
                 "line %d: %s", yylineno, message);
    }
}
