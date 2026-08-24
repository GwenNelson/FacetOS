#include <facetos/config.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_text(const char *text, FacetSystemConfig *config,
                      FacetConfigDiagnostic *diagnostic)
{
    return facet_config_parse((const uint8_t *)text, strlen(text),
                              config, diagnostic);
}

static void check_default_shape(const FacetSystemConfig *config)
{
    assert(config->version == 1);
    assert(config->logging_sink_count == 1);
    assert(strcmp(config->logging_sinks[0].name, "debug") == 0);
    assert(strcmp(config->logging_sinks[0].type,
                  "platform.sel4.debug") == 0);
    assert(config->logging_sinks[0].required);

    assert(config->seat_count == 2);
    assert(strcmp(config->seats[0].name, "seat0") == 0);
    assert(config->seats[0].type == FACET_CONFIG_SEAT_SERIAL);
    assert(config->seats[0].terminal_count == 1);
    assert(strcmp(config->seats[0].terminals[0], "ttyS0") == 0);
    assert(strcmp(config->seats[1].name, "seat1") == 0);
    assert(config->seats[1].type == FACET_CONFIG_SEAT_LOCAL);
    assert(config->seats[1].terminal_count == 2);
    assert(strcmp(config->seats[1].terminals[0], "tty1") == 0);
    assert(strcmp(config->seats[1].terminals[1], "tty2") == 0);

    assert(config->domain_count == 2);
    assert(config->root_index == 0);
    assert(config->domains[0].id == 0);
    assert(strcmp(config->domains[0].name, "system") == 0);
    assert(config->domains[0].domain_manager ==
           FACET_CONFIG_DOMAIN_MANAGER_LOCAL);
    assert(config->domains[0].logging_sink_count == 1);
    assert(config->domains[0].logging_sinks[0].sink_definition_index == 0);
    assert(config->domains[0].logging_sinks[0].level == FACET_CONFIG_LOG_DEBUG);
    assert(config->domains[0].terminal_count == 2);
    assert(config->domains[0].terminals[0].seat_index == 0);
    assert(config->domains[0].terminals[0].terminal_index == 0);
    assert(config->domains[0].terminals[1].seat_index == 1);
    assert(config->domains[0].terminals[1].terminal_index == 0);

    assert(config->domains[1].id == 1);
    assert(strcmp(config->domains[1].name, "example-child") == 0);
    assert(config->domains[1].logging_sinks[0].level == FACET_CONFIG_LOG_INFO);
    assert(config->domains[1].terminal_count == 1);
    assert(config->domains[1].terminals[0].seat_index == 1);
    assert(config->domains[1].terminals[0].terminal_index == 1);
}

static void test_fallback(void)
{
    FacetSystemConfig config;
    FacetConfigDiagnostic diagnostic;
    assert(facet_config_make_fallback(&config, &diagnostic) == 0);
    check_default_shape(&config);
    facet_config_destroy(&config);
}

static void test_packaged_config(void)
{
    FILE *file = fopen("config/facet.toml", "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    uint8_t *data = malloc((size_t)length);
    assert(data != NULL || length == 0);
    assert(fread(data, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);

    FacetSystemConfig config;
    FacetConfigDiagnostic diagnostic;
    assert(facet_config_parse(data, (size_t)length, &config, &diagnostic) == 0);
    free(data);
    check_default_shape(&config);
    facet_config_destroy(&config);
}

static void test_utf8_and_quoted_keys(void)
{
    static const char text[] =
        "['facet']\n"
        "'version' = 1\n"
        "[[logging_sinks]]\n"
        "name = \"débogage\"\n"
        "type = \"platform.sel4.debug\"\n"
        "required = true\n"
        "[[seats]]\n"
        "name = \"seat0\"\n"
        "type = \"serial\"\n"
        "terminals = [\"ttyS0\"]\n"
        "[[domains]]\n"
        "id = 0\n"
        "name = \"syst\\u00e8me\"\n"
        "personality = \"native\"\n"
        "domain_manager = \"local\"\n"
        "logging_sinks = [{ name = \"débogage\", level = \"debug\" }]\n"
        "terminals = [\"seat0.ttyS0\"]\n";
    FacetSystemConfig config;
    FacetConfigDiagnostic diagnostic;
    assert(parse_text(text, &config, &diagnostic) == 0);
    assert(strcmp(config.logging_sinks[0].name, "débogage") == 0);
    assert(strcmp(config.domains[0].name, "système") == 0);
    facet_config_destroy(&config);
}

static void expect_failure(const char *text,
                           FacetConfigDiagnosticCategory category)
{
    FacetSystemConfig config;
    FacetConfigDiagnostic diagnostic;
    assert(parse_text(text, &config, &diagnostic) != 0);
    assert(diagnostic.category == category);
}

static void expect_failure_bytes(const uint8_t *data, size_t size,
                                 FacetConfigDiagnosticCategory category)
{
    FacetSystemConfig config;
    FacetConfigDiagnostic diagnostic;
    assert(facet_config_parse(data, size, &config, &diagnostic) != 0);
    assert(diagnostic.category == category);
}

static void test_failures(void)
{
    expect_failure("[facet]\nversion = 2\n",
                   FACET_CONFIG_DIAGNOSTIC_UNSUPPORTED_VERSION);
    expect_failure("[facet]\nversion = 1\nspawn_console_server = true\n",
                   FACET_CONFIG_DIAGNOSTIC_SCHEMA);
    expect_failure("[facet]\nversion = 01\n",
                   FACET_CONFIG_DIAGNOSTIC_SYNTAX);
    expect_failure("[[domains]]\nid = 0\n[facet]\nversion = 1\n",
                   FACET_CONFIG_DIAGNOSTIC_SCHEMA);
    expect_failure("[facet]\nversion = 1\nversion = 1\n",
                   FACET_CONFIG_DIAGNOSTIC_DUPLICATE);
    expect_failure("[facet]\nversion = 1\n"
                   "[[domains]]\nid=0\nname='a'\npersonality='native'\n"
                   "domain_manager='local'\n"
                   "logging_sinks=[{name='missing',level='info'}]\n"
                   "terminals=[]\n",
                   FACET_CONFIG_DIAGNOSTIC_UNRESOLVED_REFERENCE);
    expect_failure("[facet]\nversion = 1\n"
                   "[[domains]]\nid=0\nname='a'\npersonality='native'\n"
                   "domain_manager='local'\n"
                   "logging_sinks=[{name='x',name='x',level='info'}]\n"
                   "terminals=[]\n",
                   FACET_CONFIG_DIAGNOSTIC_DUPLICATE);
    static const uint8_t malformed_utf8[] = {
        '[', 'f', 'a', 'c', 'e', 't', ']', '\n',
        'v', 'e', 'r', 's', 'i', 'o', 'n', '=', '1', '\n', 0xc0,
    };
    expect_failure_bytes(malformed_utf8, sizeof(malformed_utf8),
                         FACET_CONFIG_DIAGNOSTIC_UTF8);
    static const char duplicate_terminal[] =
        "[facet]\nversion=1\n"
        "[[logging_sinks]]\nname='d'\ntype='x'\nrequired=true\n"
        "[[seats]]\nname='s'\ntype='local'\nterminals=['t']\n"
        "[[domains]]\nid=0\nname='a'\npersonality='native'\n"
        "domain_manager='local'\nlogging_sinks=[]\nterminals=['s.t']\n"
        "[[domains]]\nid=1\nname='b'\npersonality='native'\n"
        "domain_manager='none'\nlogging_sinks=[]\nterminals=['s.t']\n";
    expect_failure(duplicate_terminal, FACET_CONFIG_DIAGNOSTIC_DUPLICATE);
}

int main(void)
{
    test_fallback();
    test_packaged_config();
    test_utf8_and_quoted_keys();
    test_failures();
    puts("config tests passed");
    return 0;
}
