#include <facetos/config.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum ValueKind {
    VALUE_STRING,
    VALUE_INTEGER,
    VALUE_BOOL,
    VALUE_ARRAY,
    VALUE_TABLE,
} ValueKind;

typedef struct Value Value;

typedef struct ValueEntry {
    char *key;
    size_t line;
    size_t column;
    Value *value;
} ValueEntry;

struct Value {
    ValueKind kind;
    size_t line;
    size_t column;
    union {
        char *string;
        int64_t integer;
        bool boolean;
        struct {
            size_t count;
            Value *items;
        } array;
        struct {
            size_t count;
            ValueEntry *entries;
        } table;
    } as;
};

typedef enum SectionKind {
    SECTION_NONE,
    SECTION_FACET,
    SECTION_LOGGING_SINK,
    SECTION_SEAT,
    SECTION_DOMAIN,
} SectionKind;

typedef struct Parser {
    const uint8_t *data;
    size_t size;
    size_t offset;
    size_t line;
    size_t column;
    size_t entry_count;
    SectionKind section;
    size_t section_index;
    bool facet_seen;
    bool failed;
    FacetSystemConfig *config;
    FacetConfigDiagnostic *diagnostic;
} Parser;

enum {
    SINK_NAME = 1u << 0,
    SINK_TYPE = 1u << 1,
    SINK_REQUIRED = 1u << 2,
    SEAT_NAME = 1u << 0,
    SEAT_TYPE = 1u << 1,
    SEAT_TERMINALS = 1u << 2,
    DOMAIN_ID = 1u << 0,
    DOMAIN_NAME = 1u << 1,
    DOMAIN_PERSONALITY = 1u << 2,
    DOMAIN_MANAGER = 1u << 3,
    DOMAIN_SINKS = 1u << 4,
    DOMAIN_TERMINALS = 1u << 5,
};

static void diagnostic_clear(FacetConfigDiagnostic *diagnostic)
{
    if (diagnostic != NULL)
        memset(diagnostic, 0, sizeof(*diagnostic));
}

static int fail_at(Parser *parser, FacetConfigDiagnosticCategory category,
                   size_t line, size_t column, const char *context,
                   const char *message)
{
    if (!parser->failed && parser->diagnostic != NULL) {
        parser->diagnostic->category = category;
        parser->diagnostic->line = line;
        parser->diagnostic->column = column;
        snprintf(parser->diagnostic->context,
                 sizeof(parser->diagnostic->context), "%s",
                 context == NULL ? "" : context);
        snprintf(parser->diagnostic->message,
                 sizeof(parser->diagnostic->message), "%s", message);
    }
    parser->failed = true;
    return -1;
}

static int fail(Parser *parser, FacetConfigDiagnosticCategory category,
                const char *context, const char *message)
{
    return fail_at(parser, category, parser->line, parser->column,
                   context, message);
}

static int consume_entry(Parser *parser, size_t line, size_t column,
                         const char *context)
{
    if (parser->entry_count >= FACET_CONFIG_MAX_ENTRIES)
        return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, line, column,
                       context, "configuration has too many entries");
    parser->entry_count++;
    return 0;
}

static int utf8_validate(const uint8_t *data, size_t size, size_t *bad_offset)
{
    size_t i = 0;
    while (i < size) {
        uint8_t first = data[i++];
        if (first < 0x80)
            continue;
        unsigned needed;
        uint32_t value;
        if (first >= 0xc2 && first <= 0xdf) {
            needed = 1;
            value = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            needed = 2;
            value = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            needed = 3;
            value = first & 0x07;
        } else {
            *bad_offset = i - 1;
            return -1;
        }
        if (needed > size - i) {
            *bad_offset = i - 1;
            return -1;
        }
        for (unsigned j = 0; j < needed; j++) {
            uint8_t next = data[i++];
            if ((next & 0xc0) != 0x80) {
                *bad_offset = i - 1;
                return -1;
            }
            value = (value << 6) | (next & 0x3f);
        }
        if ((needed == 2 && value < 0x800) ||
            (needed == 3 && value < 0x10000) ||
            value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            *bad_offset = i - needed - 1;
            return -1;
        }
    }
    return 0;
}

static int current(const Parser *parser)
{
    return parser->offset < parser->size ? parser->data[parser->offset] : -1;
}

static int take(Parser *parser)
{
    if (parser->offset >= parser->size)
        return -1;
    int c = parser->data[parser->offset++];
    if (c == '\n') {
        parser->line++;
        parser->column = 1;
    } else {
        parser->column++;
    }
    return c;
}

static void skip_space_and_comments(Parser *parser, bool allow_newline)
{
    for (;;) {
        int c = current(parser);
        if (c == ' ' || c == '\t' || c == '\r' ||
            (allow_newline && c == '\n')) {
            take(parser);
            continue;
        }
        if (c == '#') {
            while ((c = current(parser)) >= 0 && c != '\n')
                take(parser);
            if (!allow_newline)
                return;
            continue;
        }
        return;
    }
}

static char *duplicate_bytes(Parser *parser, const char *data, size_t size)
{
    if (size > FACET_CONFIG_MAX_STRING_BYTES) {
        fail(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "string",
             "string exceeds configured limit");
        return NULL;
    }
    char *result = malloc(size + 1);
    if (result == NULL) {
        fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY, "allocation",
             "out of memory");
        return NULL;
    }
    memcpy(result, data, size);
    result[size] = '\0';
    return result;
}

static int append_utf8(Parser *parser, char **buffer, size_t *used,
                       size_t *capacity, uint32_t scalar)
{
    unsigned count = scalar <= 0x7f ? 1 : scalar <= 0x7ff ? 2 :
                     scalar <= 0xffff ? 3 : 4;
    if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "string",
                    "invalid Unicode scalar escape");
    if (*used > FACET_CONFIG_MAX_STRING_BYTES - count)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "string",
                    "string exceeds configured limit");
    if (*used + count + 1 > *capacity) {
        size_t next = *capacity == 0 ? 32 : *capacity * 2;
        while (next < *used + count + 1)
            next *= 2;
        if (next > FACET_CONFIG_MAX_STRING_BYTES + 1)
            next = FACET_CONFIG_MAX_STRING_BYTES + 1;
        char *grown = realloc(*buffer, next);
        if (grown == NULL)
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                        "allocation", "out of memory");
        *buffer = grown;
        *capacity = next;
    }
    if (count == 1) {
        (*buffer)[(*used)++] = (char)scalar;
    } else if (count == 2) {
        (*buffer)[(*used)++] = (char)(0xc0 | (scalar >> 6));
        (*buffer)[(*used)++] = (char)(0x80 | (scalar & 0x3f));
    } else if (count == 3) {
        (*buffer)[(*used)++] = (char)(0xe0 | (scalar >> 12));
        (*buffer)[(*used)++] = (char)(0x80 | ((scalar >> 6) & 0x3f));
        (*buffer)[(*used)++] = (char)(0x80 | (scalar & 0x3f));
    } else {
        (*buffer)[(*used)++] = (char)(0xf0 | (scalar >> 18));
        (*buffer)[(*used)++] = (char)(0x80 | ((scalar >> 12) & 0x3f));
        (*buffer)[(*used)++] = (char)(0x80 | ((scalar >> 6) & 0x3f));
        (*buffer)[(*used)++] = (char)(0x80 | (scalar & 0x3f));
    }
    (*buffer)[*used] = '\0';
    return 0;
}

static int append_raw_byte(Parser *parser, char **buffer, size_t *used,
                           size_t *capacity, uint8_t byte)
{
    if (*used >= FACET_CONFIG_MAX_STRING_BYTES)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "string",
                    "string exceeds configured limit");
    if (*used + 2 > *capacity) {
        size_t next = *capacity == 0 ? 32 : *capacity * 2;
        if (next > FACET_CONFIG_MAX_STRING_BYTES + 1)
            next = FACET_CONFIG_MAX_STRING_BYTES + 1;
        char *grown = realloc(*buffer, next);
        if (grown == NULL)
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                        "allocation", "out of memory");
        *buffer = grown;
        *capacity = next;
    }
    (*buffer)[(*used)++] = (char)byte;
    (*buffer)[*used] = '\0';
    return 0;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char *parse_quoted_string(Parser *parser)
{
    int quote = take(parser);
    char *buffer = NULL;
    size_t used = 0, capacity = 0;
    while (current(parser) >= 0 && current(parser) != quote) {
        int c = take(parser);
        if (c == '\n' || c == '\r') {
            fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "string",
                 "multiline strings are not supported");
            free(buffer);
            return NULL;
        }
        uint32_t scalar = (uint8_t)c;
        bool escaped = false;
        if (quote == '"' && c == '\\') {
            escaped = true;
            c = take(parser);
            switch (c) {
            case 'b': scalar = '\b'; break;
            case 't': scalar = '\t'; break;
            case 'n': scalar = '\n'; break;
            case 'f': scalar = '\f'; break;
            case 'r': scalar = '\r'; break;
            case '"': scalar = '"'; break;
            case '\\': scalar = '\\'; break;
            case 'u':
            case 'U': {
                unsigned digits = c == 'u' ? 4 : 8;
                scalar = 0;
                for (unsigned i = 0; i < digits; i++) {
                    int digit = hex_digit(take(parser));
                    if (digit < 0) {
                        fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX,
                             "string", "invalid Unicode escape");
                        free(buffer);
                        return NULL;
                    }
                    scalar = (scalar << 4) | (uint32_t)digit;
                }
                break;
            }
            default:
                fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "string",
                     "invalid string escape");
                free(buffer);
                return NULL;
            }
        }
        if (scalar == 0) {
            fail(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, "string",
                 "NUL is not permitted in configuration strings");
            free(buffer);
            return NULL;
        }
        if (!escaped && ((scalar < 0x20 && scalar != '\t') || scalar == 0x7f)) {
            fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "string",
                 "unescaped control character in string");
            free(buffer);
            return NULL;
        }
        int append_result = escaped
            ? append_utf8(parser, &buffer, &used, &capacity, scalar)
            : append_raw_byte(parser, &buffer, &used, &capacity,
                              (uint8_t)scalar);
        if (append_result != 0) {
            free(buffer);
            return NULL;
        }
    }
    if (take(parser) != quote) {
        fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "string",
             "unterminated string");
        free(buffer);
        return NULL;
    }
    if (buffer == NULL)
        buffer = duplicate_bytes(parser, "", 0);
    return buffer;
}

static bool bare_key_character(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static char *parse_key(Parser *parser)
{
    if (current(parser) == '"' || current(parser) == '\'')
        return parse_quoted_string(parser);
    size_t start = parser->offset;
    while (bare_key_character(current(parser)))
        take(parser);
    if (parser->offset == start) {
        fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "key",
             "expected a key");
        return NULL;
    }
    return duplicate_bytes(parser, (const char *)parser->data + start,
                           parser->offset - start);
}

static void value_destroy(Value *value)
{
    if (value == NULL) return;
    if (value->kind == VALUE_STRING) {
        free(value->as.string);
    } else if (value->kind == VALUE_ARRAY) {
        for (size_t i = 0; i < value->as.array.count; i++)
            value_destroy(&value->as.array.items[i]);
        free(value->as.array.items);
    } else if (value->kind == VALUE_TABLE) {
        for (size_t i = 0; i < value->as.table.count; i++) {
            free(value->as.table.entries[i].key);
            value_destroy(value->as.table.entries[i].value);
            free(value->as.table.entries[i].value);
        }
        free(value->as.table.entries);
    }
    memset(value, 0, sizeof(*value));
}

static int parse_value(Parser *parser, Value *value, unsigned depth);

static int array_append(Parser *parser, Value *array, Value *item)
{
    if (array->as.array.count >= FACET_CONFIG_MAX_ARRAY_ELEMENTS)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "array",
                    "array exceeds configured element limit");
    size_t count = array->as.array.count + 1;
    Value *items = realloc(array->as.array.items, count * sizeof(*items));
    if (items == NULL)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                    "allocation", "out of memory");
    array->as.array.items = items;
    items[count - 1] = *item;
    memset(item, 0, sizeof(*item));
    array->as.array.count = count;
    return 0;
}

static int parse_array(Parser *parser, Value *value, unsigned depth)
{
    value->kind = VALUE_ARRAY;
    take(parser);
    skip_space_and_comments(parser, true);
    if (current(parser) == ']') {
        take(parser);
        return 0;
    }
    for (;;) {
        Value item = {0};
        if (parse_value(parser, &item, depth + 1) != 0) {
            value_destroy(&item);
            return -1;
        }
        if (array_append(parser, value, &item) != 0) {
            value_destroy(&item);
            return -1;
        }
        skip_space_and_comments(parser, true);
        if (current(parser) == ']') {
            take(parser);
            return 0;
        }
        if (take(parser) != ',')
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "array",
                        "expected ',' or ']'");
        skip_space_and_comments(parser, true);
        if (current(parser) == ']') {
            take(parser);
            return 0;
        }
    }
}

static int table_append(Parser *parser, Value *table, ValueEntry *entry)
{
    if (table->as.table.count >= FACET_CONFIG_MAX_ARRAY_ELEMENTS)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "inline table",
                    "inline table exceeds configured element limit");
    for (size_t i = 0; i < table->as.table.count; i++) {
        if (strcmp(table->as.table.entries[i].key, entry->key) == 0)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                           entry->line, entry->column, entry->key,
                           "duplicate inline-table key");
    }
    size_t count = table->as.table.count + 1;
    ValueEntry *entries = realloc(table->as.table.entries,
                                  count * sizeof(*entries));
    if (entries == NULL)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                    "allocation", "out of memory");
    table->as.table.entries = entries;
    entries[count - 1] = *entry;
    memset(entry, 0, sizeof(*entry));
    table->as.table.count = count;
    return 0;
}

static int parse_inline_table(Parser *parser, Value *value, unsigned depth)
{
    value->kind = VALUE_TABLE;
    take(parser);
    skip_space_and_comments(parser, false);
    if (current(parser) == '}') {
        take(parser);
        return 0;
    }
    for (;;) {
        ValueEntry entry = { .line = parser->line, .column = parser->column };
        if (consume_entry(parser, entry.line, entry.column,
                          "inline table") != 0)
            return -1;
        entry.key = parse_key(parser);
        if (entry.key == NULL) return -1;
        skip_space_and_comments(parser, false);
        if (take(parser) != '=') {
            free(entry.key);
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX,
                        "inline table", "expected '='");
        }
        skip_space_and_comments(parser, false);
        entry.value = calloc(1, sizeof(*entry.value));
        if (entry.value == NULL) {
            free(entry.key);
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                        "allocation", "out of memory");
        }
        if (parse_value(parser, entry.value, depth + 1) != 0) {
            free(entry.key);
            value_destroy(entry.value);
            free(entry.value);
            return -1;
        }
        if (table_append(parser, value, &entry) != 0) {
            free(entry.key);
            value_destroy(entry.value);
            free(entry.value);
            return -1;
        }
        skip_space_and_comments(parser, false);
        if (current(parser) == '}') {
            take(parser);
            return 0;
        }
        if (take(parser) != ',')
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX,
                        "inline table", "expected ',' or '}'");
        skip_space_and_comments(parser, false);
    }
}

static int parse_integer(Parser *parser, Value *value)
{
    bool negative = false;
    if (current(parser) == '+' || current(parser) == '-')
        negative = take(parser) == '-';
    size_t start = parser->offset;
    uint64_t magnitude = 0;
    while (current(parser) >= '0' && current(parser) <= '9') {
        unsigned digit = (unsigned)(take(parser) - '0');
        uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : INT64_MAX;
        if (magnitude > (limit - digit) / 10u)
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "integer",
                        "signed integer overflow");
        magnitude = magnitude * 10u + digit;
    }
    if (parser->offset == start)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "value",
                    "expected a value");
    if (parser->offset - start > 1 && parser->data[start] == '0')
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "integer",
                    "leading zero is not permitted");
    value->kind = VALUE_INTEGER;
    value->as.integer = negative && magnitude == (uint64_t)INT64_MAX + 1u
        ? INT64_MIN : negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return 0;
}

static int parse_value(Parser *parser, Value *value, unsigned depth)
{
    if (depth > FACET_CONFIG_MAX_DEPTH)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "value",
                    "nesting exceeds configured limit");
    value->line = parser->line;
    value->column = parser->column;
    int c = current(parser);
    if (c == '"' || c == '\'') {
        value->kind = VALUE_STRING;
        value->as.string = parse_quoted_string(parser);
        return value->as.string == NULL ? -1 : 0;
    }
    if (c == '[')
        return parse_array(parser, value, depth);
    if (c == '{')
        return parse_inline_table(parser, value, depth);
    if (parser->size - parser->offset >= 4 &&
        memcmp(parser->data + parser->offset, "true", 4) == 0) {
        parser->offset += 4;
        parser->column += 4;
        value->kind = VALUE_BOOL;
        value->as.boolean = true;
        return 0;
    }
    if (parser->size - parser->offset >= 5 &&
        memcmp(parser->data + parser->offset, "false", 5) == 0) {
        parser->offset += 5;
        parser->column += 5;
        value->kind = VALUE_BOOL;
        value->as.boolean = false;
        return 0;
    }
    return parse_integer(parser, value);
}

static int grow_array(Parser *parser, void **array, size_t count,
                      size_t element_size)
{
    if (count >= FACET_CONFIG_MAX_ARRAY_ELEMENTS ||
        count + 1 > SIZE_MAX / element_size)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "configuration",
                    "configuration array exceeds limit");
    void *grown = realloc(*array, (count + 1) * element_size);
    if (grown == NULL)
        return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                    "allocation", "out of memory");
    *array = grown;
    memset((uint8_t *)grown + count * element_size, 0, element_size);
    return 0;
}

static int parse_header(Parser *parser)
{
    take(parser);
    bool array = current(parser) == '[';
    if (array) take(parser);
    skip_space_and_comments(parser, false);
    size_t line = parser->line, column = parser->column;
    char *name = parse_key(parser);
    if (name == NULL) return -1;
    skip_space_and_comments(parser, false);
    if (take(parser) != ']' || (array && take(parser) != ']')) {
        free(name);
        return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, line, column,
                       "table", "malformed table header");
    }
    if (!parser->facet_seen && (array || strcmp(name, "facet") != 0)) {
        free(name);
        return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, line, column,
                       "facet", "[facet] must be the first table");
    }
    if (consume_entry(parser, line, column, name) != 0) {
        free(name);
        return -1;
    }
    if (!array && strcmp(name, "facet") == 0) {
        if (parser->facet_seen) {
            free(name);
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                           line, column, "facet", "duplicate [facet] table");
        }
        parser->facet_seen = true;
        parser->section = SECTION_FACET;
    } else if (array && strcmp(name, "logging_sinks") == 0) {
        size_t count = parser->config->logging_sink_count;
        if (grow_array(parser, (void **)&parser->config->logging_sinks,
                       count, sizeof(*parser->config->logging_sinks)) != 0) {
            free(name); return -1;
        }
        parser->config->logging_sink_count++;
        parser->section = SECTION_LOGGING_SINK;
        parser->section_index = count;
    } else if (array && strcmp(name, "seats") == 0) {
        size_t count = parser->config->seat_count;
        if (grow_array(parser, (void **)&parser->config->seats,
                       count, sizeof(*parser->config->seats)) != 0) {
            free(name); return -1;
        }
        parser->config->seat_count++;
        parser->section = SECTION_SEAT;
        parser->section_index = count;
    } else if (array && strcmp(name, "domains") == 0) {
        size_t count = parser->config->domain_count;
        if (grow_array(parser, (void **)&parser->config->domains,
                       count, sizeof(*parser->config->domains)) != 0) {
            free(name); return -1;
        }
        parser->config->domain_count++;
        parser->section = SECTION_DOMAIN;
        parser->section_index = count;
    } else {
        int result = fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                             line, column, name, "unknown configuration table");
        free(name);
        return result;
    }
    free(name);
    return 0;
}

static int require_kind(Parser *parser, const char *key, const Value *value,
                        ValueKind kind, const char *type_name)
{
    if (value->kind == kind) return 0;
    char message[128];
    snprintf(message, sizeof(message), "expected %s", type_name);
    return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                   value->line, value->column, key, message);
}

static int take_string(Parser *parser, const char *key, Value *value,
                       char **destination)
{
    if (require_kind(parser, key, value, VALUE_STRING, "string") != 0)
        return -1;
    *destination = value->as.string;
    value->as.string = NULL;
    return 0;
}

static int parse_log_level(Parser *parser, const char *key, const Value *value,
                           FacetConfigLogLevel *level)
{
    if (require_kind(parser, key, value, VALUE_STRING, "log-level string") != 0)
        return -1;
    struct { const char *name; FacetConfigLogLevel level; } levels[] = {
        { "none", FACET_CONFIG_LOG_NONE }, { "fatal", FACET_CONFIG_LOG_FATAL },
        { "error", FACET_CONFIG_LOG_ERROR }, { "warning", FACET_CONFIG_LOG_WARNING },
        { "info", FACET_CONFIG_LOG_INFO }, { "debug", FACET_CONFIG_LOG_DEBUG },
        { "trace", FACET_CONFIG_LOG_TRACE },
    };
    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        if (strcmp(value->as.string, levels[i].name) == 0) {
            *level = levels[i].level;
            return 0;
        }
    }
    return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                   value->line, value->column, key, "invalid log level");
}

static int strings_from_array(Parser *parser, const char *key, Value *value,
                              char ***strings, size_t *count)
{
    if (require_kind(parser, key, value, VALUE_ARRAY, "array") != 0)
        return -1;
    for (size_t i = 0; i < value->as.array.count; i++) {
        if (require_kind(parser, key, &value->as.array.items[i],
                         VALUE_STRING, "string array") != 0)
            return -1;
    }
    if (value->as.array.count != 0) {
        *strings = calloc(value->as.array.count, sizeof(**strings));
        if (*strings == NULL)
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                        key, "out of memory");
    }
    for (size_t i = 0; i < value->as.array.count; i++) {
        Value *item = &value->as.array.items[i];
        (*strings)[i] = item->as.string;
        item->as.string = NULL;
    }
    *count = value->as.array.count;
    return 0;
}

static const Value *table_value(const Value *table, const char *wanted,
                                bool *seen)
{
    for (size_t i = 0; i < table->as.table.count; i++) {
        const ValueEntry *entry = &table->as.table.entries[i];
        if (strcmp(entry->key, wanted) == 0) {
            *seen = true;
            return entry->value;
        }
    }
    return NULL;
}

static int domain_sinks_from_array(Parser *parser, Value *value,
                                   FacetConfigDomain *domain)
{
    if (require_kind(parser, "logging_sinks", value, VALUE_ARRAY, "array") != 0)
        return -1;
    size_t count = value->as.array.count;
    if (count != 0) {
        domain->logging_sinks = calloc(count, sizeof(*domain->logging_sinks));
        if (domain->logging_sinks == NULL)
            return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                        "logging_sinks", "out of memory");
    }
    domain->logging_sink_count = count;
    for (size_t i = 0; i < count; i++) {
        Value *item = &value->as.array.items[i];
        if (require_kind(parser, "logging_sinks", item, VALUE_TABLE,
                         "inline table") != 0)
            return -1;
        bool name_seen = false, level_seen = false;
        const Value *name = table_value(item, "name", &name_seen);
        const Value *level = table_value(item, "level", &level_seen);
        if (!name_seen || !level_seen || item->as.table.count != 2)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                           item->line, item->column, "logging_sinks",
                           "sink use requires only name and level");
        if (require_kind(parser, "name", name, VALUE_STRING, "string") != 0)
            return -1;
        domain->logging_sinks[i].name = duplicate_bytes(
            parser, name->as.string, strlen(name->as.string));
        if (domain->logging_sinks[i].name == NULL) return -1;
        if (parse_log_level(parser, "level", level,
                            &domain->logging_sinks[i].level) != 0)
            return -1;
    }
    return 0;
}

static int apply_value(Parser *parser, char *key, Value *value,
                       size_t line, size_t column)
{
    if (consume_entry(parser, line, column, key) != 0)
        return -1;
    if (parser->section == SECTION_FACET) {
        if (strcmp(key, "version") != 0)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                           line, column, key, "unknown [facet] key");
        if (parser->config->version != 0)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                           line, column, key, "duplicate version");
        if (require_kind(parser, key, value, VALUE_INTEGER, "integer") != 0)
            return -1;
        if (value->as.integer != 1)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_UNSUPPORTED_VERSION,
                           value->line, value->column, key,
                           "unsupported configuration version");
        parser->config->version = 1;
        return 0;
    }
    if (parser->section == SECTION_LOGGING_SINK) {
        FacetConfigLoggingSinkDefinition *sink =
            &parser->config->logging_sinks[parser->section_index];
        uint32_t bit;
        if (strcmp(key, "name") == 0) bit = SINK_NAME;
        else if (strcmp(key, "type") == 0) bit = SINK_TYPE;
        else if (strcmp(key, "required") == 0) bit = SINK_REQUIRED;
        else return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                            line, column, key, "unknown logging sink key");
        if (sink->_present & bit)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                           line, column, key, "duplicate logging sink key");
        sink->_present |= bit;
        if (bit == SINK_NAME) return take_string(parser, key, value, &sink->name);
        if (bit == SINK_TYPE) return take_string(parser, key, value, &sink->type);
        if (require_kind(parser, key, value, VALUE_BOOL, "boolean") != 0)
            return -1;
        sink->required = value->as.boolean;
        return 0;
    }
    if (parser->section == SECTION_SEAT) {
        FacetConfigSeatDefinition *seat =
            &parser->config->seats[parser->section_index];
        uint32_t bit;
        if (strcmp(key, "name") == 0) bit = SEAT_NAME;
        else if (strcmp(key, "type") == 0) bit = SEAT_TYPE;
        else if (strcmp(key, "terminals") == 0) bit = SEAT_TERMINALS;
        else return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                            line, column, key, "unknown seat key");
        if (seat->_present & bit)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                           line, column, key, "duplicate seat key");
        seat->_present |= bit;
        if (bit == SEAT_NAME) return take_string(parser, key, value, &seat->name);
        if (bit == SEAT_TERMINALS)
            return strings_from_array(parser, key, value, &seat->terminals,
                                      &seat->terminal_count);
        if (require_kind(parser, key, value, VALUE_STRING, "string") != 0)
            return -1;
        if (strcmp(value->as.string, "serial") == 0)
            seat->type = FACET_CONFIG_SEAT_SERIAL;
        else if (strcmp(value->as.string, "local") == 0)
            seat->type = FACET_CONFIG_SEAT_LOCAL;
        else return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                            value->line, value->column, key,
                            "seat type must be serial or local");
        return 0;
    }
    if (parser->section == SECTION_DOMAIN) {
        FacetConfigDomain *domain =
            &parser->config->domains[parser->section_index];
        uint32_t bit;
        if (strcmp(key, "id") == 0) bit = DOMAIN_ID;
        else if (strcmp(key, "name") == 0) bit = DOMAIN_NAME;
        else if (strcmp(key, "personality") == 0) bit = DOMAIN_PERSONALITY;
        else if (strcmp(key, "domain_manager") == 0) bit = DOMAIN_MANAGER;
        else if (strcmp(key, "logging_sinks") == 0) bit = DOMAIN_SINKS;
        else if (strcmp(key, "terminals") == 0) bit = DOMAIN_TERMINALS;
        else return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                            line, column, key, "unknown domain key");
        if (domain->_present & bit)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                           line, column, key, "duplicate domain key");
        domain->_present |= bit;
        if (bit == DOMAIN_NAME) return take_string(parser, key, value, &domain->name);
        if (bit == DOMAIN_SINKS)
            return domain_sinks_from_array(parser, value, domain);
        if (bit == DOMAIN_TERMINALS) {
            char **references = NULL;
            size_t count = 0;
            if (strings_from_array(parser, key, value, &references, &count) != 0)
                return -1;
            if (count != 0) {
                domain->terminals = calloc(count, sizeof(*domain->terminals));
                if (domain->terminals == NULL) {
                    for (size_t i = 0; i < count; i++) free(references[i]);
                    free(references);
                    return fail(parser, FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
                                key, "out of memory");
                }
            }
            for (size_t i = 0; i < count; i++)
                domain->terminals[i].reference = references[i];
            free(references);
            domain->terminal_count = count;
            return 0;
        }
        if (bit == DOMAIN_ID) {
            if (require_kind(parser, key, value, VALUE_INTEGER,
                             "non-negative integer") != 0)
                return -1;
            if (value->as.integer < 0)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                               value->line, value->column, key,
                               "domain id must be non-negative");
            domain->id = (uint64_t)value->as.integer;
            return 0;
        }
        if (require_kind(parser, key, value, VALUE_STRING, "string") != 0)
            return -1;
        if (bit == DOMAIN_PERSONALITY) {
            if (strcmp(value->as.string, "native") == 0)
                domain->personality = FACET_CONFIG_PERSONALITY_NATIVE;
            else if (strcmp(value->as.string, "posix") == 0)
                domain->personality = FACET_CONFIG_PERSONALITY_POSIX;
            else if (strcmp(value->as.string, "vm") == 0)
                domain->personality = FACET_CONFIG_PERSONALITY_VM;
            else return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                                value->line, value->column, key,
                                "invalid domain personality");
        } else {
            if (strcmp(value->as.string, "none") == 0)
                domain->domain_manager = FACET_CONFIG_DOMAIN_MANAGER_NONE;
            else if (strcmp(value->as.string, "local") == 0)
                domain->domain_manager = FACET_CONFIG_DOMAIN_MANAGER_LOCAL;
            else if (strcmp(value->as.string, "parent") == 0)
                domain->domain_manager = FACET_CONFIG_DOMAIN_MANAGER_PARENT;
            else return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA,
                                value->line, value->column, key,
                                "invalid domain manager mode");
        }
        return 0;
    }
    return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, line, column,
                   key, "assignment appears before a table header");
}

static int validate_required(Parser *parser)
{
    if (!parser->facet_seen || parser->config->version != 1)
        return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                       "facet.version", "[facet] version = 1 is required");
    for (size_t i = 0; i < parser->config->logging_sink_count; i++) {
        FacetConfigLoggingSinkDefinition *sink = &parser->config->logging_sinks[i];
        if ((sink->_present & (SINK_NAME | SINK_TYPE | SINK_REQUIRED)) !=
            (SINK_NAME | SINK_TYPE | SINK_REQUIRED))
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                           "logging_sinks", "sink requires name, type, and required");
        if (sink->name[0] == '\0' || sink->type[0] == '\0')
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                           "logging_sinks", "sink name and type must not be empty");
        for (size_t j = 0; j < i; j++)
            if (strcmp(sink->name, parser->config->logging_sinks[j].name) == 0)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE, 0, 0,
                               sink->name, "duplicate logging sink name");
    }
    for (size_t i = 0; i < parser->config->seat_count; i++) {
        FacetConfigSeatDefinition *seat = &parser->config->seats[i];
        if ((seat->_present & (SEAT_NAME | SEAT_TYPE | SEAT_TERMINALS)) !=
            (SEAT_NAME | SEAT_TYPE | SEAT_TERMINALS))
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                           "seats", "seat requires name, type, and terminals");
        if (seat->name[0] == '\0' || strchr(seat->name, '.') != NULL)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                           "seats", "seat name must be nonempty and contain no dot");
        for (size_t j = 0; j < i; j++)
            if (strcmp(seat->name, parser->config->seats[j].name) == 0)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE, 0, 0,
                               seat->name, "duplicate seat name");
        for (size_t j = 0; j < seat->terminal_count; j++) {
            if (seat->terminals[j][0] == '\0' || strchr(seat->terminals[j], '.') != NULL)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                               seat->name, "terminal name must be nonempty and contain no dot");
            for (size_t k = 0; k < j; k++)
                if (strcmp(seat->terminals[j], seat->terminals[k]) == 0)
                    return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE, 0, 0,
                                   seat->terminals[j], "duplicate terminal in seat");
        }
    }
    if (parser->config->domain_count == 0)
        return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                       "domains", "at least one domain is required");
    size_t roots = 0;
    for (size_t i = 0; i < parser->config->domain_count; i++) {
        FacetConfigDomain *domain = &parser->config->domains[i];
        uint32_t required = DOMAIN_ID | DOMAIN_NAME | DOMAIN_PERSONALITY |
                            DOMAIN_MANAGER | DOMAIN_SINKS | DOMAIN_TERMINALS;
        if ((domain->_present & required) != required)
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                           "domains", "domain is missing a required key");
        if (domain->name[0] == '\0')
            return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                           "domains.name", "domain name must not be empty");
        if (domain->id == 0) {
            roots++;
            parser->config->root_index = i;
        }
        for (size_t j = 0; j < i; j++) {
            if (domain->id == parser->config->domains[j].id)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE, 0, 0,
                               domain->name, "duplicate domain id");
            if (strcmp(domain->name, parser->config->domains[j].name) == 0)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE, 0, 0,
                               domain->name, "duplicate domain name");
        }
        for (size_t j = 0; j < domain->logging_sink_count; j++) {
            FacetConfigDomainSink *use = &domain->logging_sinks[j];
            bool found = false;
            for (size_t k = 0; k < parser->config->logging_sink_count; k++) {
                if (strcmp(use->name, parser->config->logging_sinks[k].name) == 0) {
                    use->sink_definition_index = k;
                    found = true;
                    break;
                }
            }
            if (!found)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_UNRESOLVED_REFERENCE,
                               0, 0, use->name, "unknown logging sink");
            for (size_t k = 0; k < j; k++)
                if (strcmp(use->name, domain->logging_sinks[k].name) == 0)
                    return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                                   0, 0, use->name,
                                   "domain references a logging sink twice");
        }
        for (size_t j = 0; j < domain->terminal_count; j++) {
            FacetConfigTerminalAssignment *assignment = &domain->terminals[j];
            char *dot = strchr(assignment->reference, '.');
            if (dot == NULL || dot == assignment->reference || dot[1] == '\0' ||
                strchr(dot + 1, '.') != NULL)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                               assignment->reference,
                               "terminal reference must be seat.terminal");
            size_t seat_length = (size_t)(dot - assignment->reference);
            bool found = false;
            for (size_t k = 0; k < parser->config->seat_count; k++) {
                FacetConfigSeatDefinition *seat = &parser->config->seats[k];
                if (strlen(seat->name) != seat_length ||
                    memcmp(seat->name, assignment->reference, seat_length) != 0)
                    continue;
                for (size_t m = 0; m < seat->terminal_count; m++) {
                    if (strcmp(seat->terminals[m], dot + 1) == 0) {
                        assignment->seat_index = k;
                        assignment->terminal_index = m;
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
                return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_UNRESOLVED_REFERENCE,
                               0, 0, assignment->reference,
                               "unknown seat terminal");
            for (size_t di = 0; di <= i; di++) {
                size_t limit = di == i ? j : parser->config->domains[di].terminal_count;
                for (size_t ti = 0; ti < limit; ti++) {
                    FacetConfigTerminalAssignment *other =
                        &parser->config->domains[di].terminals[ti];
                    if (other->seat_index == assignment->seat_index &&
                        other->terminal_index == assignment->terminal_index)
                        return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
                                       0, 0, assignment->reference,
                                       "terminal is assigned to more than one domain");
                }
            }
        }
    }
    if (roots != 1)
        return fail_at(parser, FACET_CONFIG_DIAGNOSTIC_SCHEMA, 0, 0,
                       "domains", "exactly one domain must have id 0");
    return 0;
}

int facet_config_parse(const uint8_t *data, size_t size,
                       FacetSystemConfig *config,
                       FacetConfigDiagnostic *diagnostic)
{
    diagnostic_clear(diagnostic);
    if (config == NULL || (data == NULL && size != 0)) return -1;
    memset(config, 0, sizeof(*config));
    Parser parser = {
        .data = data, .size = size, .line = 1, .column = 1,
        .config = config, .diagnostic = diagnostic,
    };
    if (size > FACET_CONFIG_MAX_BYTES) {
        fail(&parser, FACET_CONFIG_DIAGNOSTIC_LIMIT, "file",
             "configuration exceeds 1 MiB limit");
        return -1;
    }
    size_t bad_offset;
    if (utf8_validate(data, size, &bad_offset) != 0) {
        size_t line = 1, column = 1;
        for (size_t i = 0; i < bad_offset; i++) {
            if (data[i] == '\n') { line++; column = 1; }
            else column++;
        }
        fail_at(&parser, FACET_CONFIG_DIAGNOSTIC_UTF8, line, column,
                "file", "configuration is not valid UTF-8");
        return -1;
    }
    skip_space_and_comments(&parser, true);
    while (current(&parser) >= 0) {
        if (current(&parser) == '[') {
            if (parse_header(&parser) != 0) goto error;
        } else {
            size_t line = parser.line, column = parser.column;
            char *key = parse_key(&parser);
            if (key == NULL) goto error;
            skip_space_and_comments(&parser, false);
            if (take(&parser) != '=') {
                free(key);
                fail_at(&parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX,
                        line, column, "assignment", "expected '='");
                goto error;
            }
            skip_space_and_comments(&parser, false);
            Value value = {0};
            if (parse_value(&parser, &value, 0) != 0) {
                free(key);
                value_destroy(&value);
                goto error;
            }
            if (apply_value(&parser, key, &value, line, column) != 0) {
                free(key);
                value_destroy(&value);
                goto error;
            }
            free(key);
            value_destroy(&value);
        }
        skip_space_and_comments(&parser, false);
        if (current(&parser) == '#')
            skip_space_and_comments(&parser, false);
        if (current(&parser) >= 0 && current(&parser) != '\n') {
            fail(&parser, FACET_CONFIG_DIAGNOSTIC_SYNTAX, "line",
                 "unexpected data after value or table header");
            goto error;
        }
        skip_space_and_comments(&parser, true);
    }
    if (validate_required(&parser) != 0) goto error;
    return 0;
error:
    facet_config_destroy(config);
    return -1;
}

int facet_config_make_fallback(FacetSystemConfig *config,
                               FacetConfigDiagnostic *diagnostic)
{
    static const char fallback[] =
        "[facet]\n"
        "version = 1\n"
        "[[logging_sinks]]\n"
        "name = \"debug\"\n"
        "type = \"platform.sel4.debug\"\n"
        "required = true\n"
        "[[seats]]\n"
        "name = \"seat0\"\n"
        "type = \"serial\"\n"
        "terminals = [\"ttyS0\"]\n"
        "[[seats]]\n"
        "name = \"seat1\"\n"
        "type = \"local\"\n"
        "terminals = [\"tty1\", \"tty2\"]\n"
        "[[domains]]\n"
        "id = 0\n"
        "name = \"system\"\n"
        "personality = \"native\"\n"
        "domain_manager = \"local\"\n"
        "logging_sinks = [{ name = \"debug\", level = \"debug\" }]\n"
        "terminals = [\"seat0.ttyS0\", \"seat1.tty1\"]\n"
        "[[domains]]\n"
        "id = 1\n"
        "name = \"example-child\"\n"
        "personality = \"native\"\n"
        "domain_manager = \"none\"\n"
        "logging_sinks = [{ name = \"debug\", level = \"info\" }]\n"
        "terminals = [\"seat1.tty2\"]\n";
    return facet_config_parse((const uint8_t *)fallback,
                              sizeof(fallback) - 1, config, diagnostic);
}

void facet_config_destroy(FacetSystemConfig *config)
{
    if (config == NULL) return;
    for (size_t i = 0; i < config->logging_sink_count; i++) {
        free(config->logging_sinks[i].name);
        free(config->logging_sinks[i].type);
    }
    free(config->logging_sinks);
    for (size_t i = 0; i < config->seat_count; i++) {
        free(config->seats[i].name);
        for (size_t j = 0; j < config->seats[i].terminal_count; j++)
            free(config->seats[i].terminals[j]);
        free(config->seats[i].terminals);
    }
    free(config->seats);
    for (size_t i = 0; i < config->domain_count; i++) {
        free(config->domains[i].name);
        for (size_t j = 0; j < config->domains[i].logging_sink_count; j++)
            free(config->domains[i].logging_sinks[j].name);
        free(config->domains[i].logging_sinks);
        for (size_t j = 0; j < config->domains[i].terminal_count; j++)
            free(config->domains[i].terminals[j].reference);
        free(config->domains[i].terminals);
    }
    free(config->domains);
    memset(config, 0, sizeof(*config));
}

const char *facet_config_diagnostic_category_name(
    FacetConfigDiagnosticCategory category)
{
    switch (category) {
    case FACET_CONFIG_DIAGNOSTIC_NONE: return "none";
    case FACET_CONFIG_DIAGNOSTIC_SYNTAX: return "syntax";
    case FACET_CONFIG_DIAGNOSTIC_UTF8: return "utf8";
    case FACET_CONFIG_DIAGNOSTIC_LIMIT: return "limit";
    case FACET_CONFIG_DIAGNOSTIC_UNSUPPORTED_VERSION: return "unsupported-version";
    case FACET_CONFIG_DIAGNOSTIC_SCHEMA: return "schema";
    case FACET_CONFIG_DIAGNOSTIC_DUPLICATE: return "duplicate";
    case FACET_CONFIG_DIAGNOSTIC_UNRESOLVED_REFERENCE: return "unresolved-reference";
    case FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY: return "out-of-memory";
    }
    return "unknown";
}
