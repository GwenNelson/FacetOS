%option noyywrap
%option yylineno

%{
#include "parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void yyerror(const char *message);
%}

IDENTIFIER [A-Za-z_][A-Za-z0-9_]*
NUMBERVAL  [0-9]+

%%

"interface"      return INTERFACE;
"uuid"           return UUID;
"requires"       return REQUIRES;
"method"         return METHOD;
"property"       return PROPERTY;
"read"           return READ;
"write"          return WRITE;
"inout"          return INOUT;
"in"             return IN;
"out"            return OUT;

{NUMBERVAL} {
    yylval.number = (uint32_t)strtoul(yytext, NULL, 10);
    return NUMBER;
}

{IDENTIFIER} {
    yylval.string = strdup(yytext);
    return IDENT;
}

\"([^\"\\]|\\.)*\" {
    size_t length = yyleng - 2;
    yylval.string = malloc(length + 1);
    if (yylval.string == NULL) {
        yyerror("out of memory while reading string");
        return 0;
    }
    memcpy(yylval.string, yytext + 1, length);
    yylval.string[length] = '\0';
    return STRING;
}

[ \t\r\n]+       ;
"//"[^\n]*       ;

.                return yytext[0];
