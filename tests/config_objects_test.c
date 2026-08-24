#include <facetos/dominit0/config.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void check_string(FacetString string, const char *expected)
{
    assert(string.length == strlen(expected));
    assert(memcmp(string.data, expected, string.length) == 0);
}

int main(void)
{
    FacetSystemConfig parsed;
    FacetConfigDiagnostic diagnostic;
    Dominit0SystemConfig system;
    assert(facet_config_make_fallback(&parsed, &diagnostic) == 0);
    assert(dominit0_config_objects_init(&system, &parsed, &diagnostic) == 0);
    assert(parsed.domain_count == 0);
    assert(system.domain_count == 2);
    assert(system.root_index == 0);

    Dominit0DomainConfigObject *root = &system.domains[0];
    uint64_t id = UINT64_MAX;
    FacetString name = {0};
    Personality personality = Personality_Vm;
    DomainManagerMode manager = DomainManagerMode_None;
    FacetArray_Sink sinks = {0};
    FacetArray_Assignment assignments = {0};
    assert(root->domain.getdomain_id(root->domain.self, &id) == FACET_OK);
    assert(id == 0);
    assert(root->domain.getdomain_name(root->domain.self, &name) == FACET_OK);
    check_string(name, "system");
    assert(root->domain.getpersonality(root->domain.self, &personality) == FACET_OK);
    assert(personality == Personality_Native);
    assert(root->domain.getdomain_manager(root->domain.self, &manager) == FACET_OK);
    assert(manager == DomainManagerMode_Local);
    assert(root->logging.getsinks(root->logging.self, &sinks) == FACET_OK);
    assert(sinks.count == 1);
    check_string(sinks.data[0].name, "debug");
    assert(sinks.data[0].level == LogLevel_Debug);
    assert(root->console.getassignments(root->console.self, &assignments) == FACET_OK);
    assert(assignments.count == 2);
    check_string(assignments.data[0].seat, "seat0");
    check_string(assignments.data[0].terminal, "ttyS0");
    check_string(assignments.data[1].seat, "seat1");
    check_string(assignments.data[1].terminal, "tty1");

    Dominit0DomainConfigObject *child = &system.domains[1];
    assert(child->console.getassignments(child->console.self,
                                         &assignments) == FACET_OK);
    assert(assignments.count == 1);
    check_string(assignments.data[0].seat, "seat1");
    check_string(assignments.data[0].terminal, "tty2");

    FacetHandle result = {0};
    assert(root->domain.getlogger_config(root->domain.self, &result) ==
           FACET_INVALID_HANDLE);
    assert(root->domain.getInterface(root->domain.self, IID_ILoggingConfig,
                                     &result) == FACET_INVALID_HANDLE);

    FacetHandle domain_handle = { .platform = (void *)(uintptr_t)1 };
    FacetHandle logging_handle = { .platform = (void *)(uintptr_t)2 };
    FacetHandle console_handle = { .platform = (void *)(uintptr_t)3 };
    assert(dominit0_domain_config_bind_handles(root, domain_handle,
                                               logging_handle,
                                               console_handle) == 0);
    assert(root->domain.getInterface(root->domain.self, IID_ILoggingConfig,
                                     &result) == FACET_OK);
    assert(result.platform == logging_handle.platform);
    assert(root->domain.getInterface(root->domain.self,
                                     IID_IDomainConsoleConfig,
                                     &result) == FACET_OK);
    assert(result.platform == console_handle.platform);

    uuid_t unknown = { .bytes = {0xff} };
    assert(root->domain.getInterface(root->domain.self, unknown, &result) ==
           FACET_NO_INTERFACE);

    dominit0_config_objects_destroy(&system);
    return 0;
}
