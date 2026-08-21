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
void yyerror(const char *message);
%}

%union {
    char *string;
    uint32_t number;
}

%token INTERFACE UUID REQUIRES METHOD PROPERTY READ WRITE
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
    UUID STRING ';'
    {
        snprintf(facet_idl_parser_context->interface.uuid,
                 sizeof(facet_idl_parser_context->interface.uuid),
                 "%s", $2);
        free($2);
    }
  | REQUIRES IDENT ';'
    {
        if (facet_idl_add_required(facet_idl_parser_context, $2) != 0) {
            yyerror("too many required interfaces");
        }
        free($2);
    }
  | METHOD NUMBER return_type IDENT '('
    {
        facet_idl_current_method = facet_idl_add_method(
            facet_idl_parser_context, $2, $3, $4);
        if (facet_idl_current_method == NULL) {
            yyerror("too many methods");
        }
    }
    parameters ')' ';'
    {
        free($3);
        free($4);
        facet_idl_current_method = NULL;
    }
  | PROPERTY NUMBER type_name IDENT property_access ';'
    {
        if (facet_idl_add_property(
                facet_idl_parser_context, $2, $3, $4,
                ($5 & 1u) != 0, ($5 & 2u) != 0) != 0) {
            yyerror("invalid property");
        }
        free($3);
        free($4);
    }
;

return_type:
    IDENT { $$ = $1; }
;

type_name:
    IDENT { $$ = $1; }
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
