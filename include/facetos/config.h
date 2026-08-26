#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FACET_CONFIG_MAX_BYTES (1024u * 1024u)
#define FACET_CONFIG_MAX_DEPTH 16u
#define FACET_CONFIG_MAX_ENTRIES 4096u
#define FACET_CONFIG_MAX_STRING_BYTES (64u * 1024u)
#define FACET_CONFIG_MAX_ARRAY_ELEMENTS 4096u

typedef enum FacetConfigDiagnosticCategory {
    FACET_CONFIG_DIAGNOSTIC_NONE = 0,
    FACET_CONFIG_DIAGNOSTIC_SYNTAX,
    FACET_CONFIG_DIAGNOSTIC_UTF8,
    FACET_CONFIG_DIAGNOSTIC_LIMIT,
    FACET_CONFIG_DIAGNOSTIC_UNSUPPORTED_VERSION,
    FACET_CONFIG_DIAGNOSTIC_SCHEMA,
    FACET_CONFIG_DIAGNOSTIC_DUPLICATE,
    FACET_CONFIG_DIAGNOSTIC_UNRESOLVED_REFERENCE,
    FACET_CONFIG_DIAGNOSTIC_OUT_OF_MEMORY,
} FacetConfigDiagnosticCategory;

typedef struct FacetConfigDiagnostic {
    FacetConfigDiagnosticCategory category;
    size_t line;
    size_t column;
    char context[96];
    char message[192];
} FacetConfigDiagnostic;

typedef enum FacetConfigPersonality {
    FACET_CONFIG_PERSONALITY_NATIVE = 0,
    FACET_CONFIG_PERSONALITY_POSIX = 1,
    FACET_CONFIG_PERSONALITY_VM = 2,
} FacetConfigPersonality;

typedef enum FacetConfigDomainManagerMode {
    FACET_CONFIG_DOMAIN_MANAGER_NONE = 0,
    FACET_CONFIG_DOMAIN_MANAGER_LOCAL = 1,
    FACET_CONFIG_DOMAIN_MANAGER_PARENT = 2,
} FacetConfigDomainManagerMode;

typedef enum FacetConfigLogLevel {
    FACET_CONFIG_LOG_NONE = 0,
    FACET_CONFIG_LOG_FATAL = 10,
    FACET_CONFIG_LOG_ERROR = 20,
    FACET_CONFIG_LOG_WARNING = 30,
    FACET_CONFIG_LOG_INFO = 40,
    FACET_CONFIG_LOG_DEBUG = 50,
    FACET_CONFIG_LOG_TRACE = 60,
} FacetConfigLogLevel;

typedef enum FacetConfigSeatType {
    FACET_CONFIG_SEAT_SERIAL = 0,
    FACET_CONFIG_SEAT_LOCAL = 1,
} FacetConfigSeatType;

typedef enum FacetConfigTerminalView {
    FACET_CONFIG_TERMINAL_VIEW_NATIVE = 0,
    FACET_CONFIG_TERMINAL_VIEW_POSIX = 1,
} FacetConfigTerminalView;

typedef struct FacetConfigLoggingSinkDefinition {
    char *name;
    char *type;
    bool required;
    uint32_t _present;
} FacetConfigLoggingSinkDefinition;

typedef struct FacetConfigSeatDefinition {
    char *name;
    char *server;
    FacetConfigSeatType type;
    size_t terminal_count;
    char **terminals;
    uint32_t _present;
} FacetConfigSeatDefinition;

typedef struct FacetConfigAuthenticationSource {
    char *name;
    uint64_t provider_domain_id;
    size_t provider_domain_index;
    char *type;
    bool required;
    uint32_t _present;
} FacetConfigAuthenticationSource;

typedef struct FacetConfigUser {
    char *name;
    char *password_sha256;
    bool admin;
    uint32_t uid;
    uint32_t gid;
    char *home_path;
    char *native_shell;
    char *posix_shell;
    uint32_t _present;
} FacetConfigUser;

typedef struct FacetConfigDomainSink {
    char *name;
    FacetConfigLogLevel level;
    size_t sink_definition_index;
} FacetConfigDomainSink;

typedef struct FacetConfigTerminalAssignment {
    char *reference;
    size_t seat_index;
    size_t terminal_index;
    FacetConfigTerminalView view;
    char *initial_process;
    char *device_name;
    char *run_as;
    uint32_t _present;
} FacetConfigTerminalAssignment;

typedef struct FacetConfigDomain {
    uint64_t id;
    char *name;
    FacetConfigPersonality personality;
    FacetConfigDomainManagerMode domain_manager;
    char *authentication_source;
    size_t authentication_source_index;
    char *pid1;
    char *initrd;
    size_t user_count;
    FacetConfigUser *users;
    size_t logging_sink_count;
    FacetConfigDomainSink *logging_sinks;
    size_t terminal_count;
    FacetConfigTerminalAssignment *terminals;
    uint32_t _present;
} FacetConfigDomain;

typedef struct FacetSystemConfig {
    uint64_t version;
    char *seat_initrd;
    size_t logging_sink_count;
    FacetConfigLoggingSinkDefinition *logging_sinks;
    size_t authentication_source_count;
    FacetConfigAuthenticationSource *authentication_sources;
    size_t user_count;
    FacetConfigUser *users;
    size_t seat_count;
    FacetConfigSeatDefinition *seats;
    size_t domain_count;
    FacetConfigDomain *domains;
    size_t root_index;
} FacetSystemConfig;

int facet_config_parse(const uint8_t *data, size_t size,
                       FacetSystemConfig *config,
                       FacetConfigDiagnostic *diagnostic);

int facet_config_make_fallback(FacetSystemConfig *config,
                               FacetConfigDiagnostic *diagnostic);

void facet_config_destroy(FacetSystemConfig *config);

const char *facet_config_diagnostic_category_name(
    FacetConfigDiagnosticCategory category);
