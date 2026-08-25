#include <facetos/config.h>
#include <facetos/dominit0/config.h>
#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/logging.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/dominit0/platform/api.h>

#ifdef DEBUG
void test_kmalloc(void) {
     kmalloc_dump();
     klog(LOG_DEBUG,"test_kmalloc() - allocating 17kb buffer\n");
     void* buf_a = kmalloc(17 * 1024);
     klog(LOG_DEBUG,"test_kmalloc() - got buffer at %p\n",buf_a);
     kmalloc_dump();
     klog(LOG_DEBUG,"test_kmalloc() - allocation 13kb buffer\n");
     void* buf_b = kmalloc(13 * 1024);
     klog(LOG_DEBUG,"test_kmalloc() - got buffer at %p\n",buf_b);
     kmalloc_dump();
     klog(LOG_DEBUG,"test_kmalloc() - freeing both buffers...\n");
     kfree(buf_a);
     kfree(buf_b);
     kmalloc_dump();
}
#endif

static void log_config_diagnostic(const FacetConfigDiagnostic *diagnostic)
{
     klog(LOG_ERROR,
          "Configuration %s error at %zu:%zu (%s): %s\n",
          facet_config_diagnostic_category_name(diagnostic->category),
          diagnostic->line,
          diagnostic->column,
          diagnostic->context,
          diagnostic->message);
}

static void log_prepared_configuration(const Dominit0SystemConfig *system,
                                       bool fallback)
{
     klog(LOG_INFO,
          "Prepared %zu domain configuration object%s%s\n",
          system->domain_count,
          system->domain_count == 1 ? "" : "s",
          fallback ? " from built-in fallback defaults" : " from facet.toml");
     for (size_t i = 0; i < system->domain_count; i++) {
          const FacetConfigDomain *domain = &system->parsed.domains[i];
          klog(LOG_INFO,
               "Domain %llu (%s): %zu logging sink%s, %zu terminal%s\n",
               (unsigned long long)domain->id,
               domain->name,
               domain->logging_sink_count,
               domain->logging_sink_count == 1 ? "" : "s",
               domain->terminal_count,
               domain->terminal_count == 1 ? "" : "s");
          for (size_t j = 0; j < domain->terminal_count; j++)
               klog(LOG_INFO, "  terminal: %s\n",
                    domain->terminals[j].reference);
     }
}

static void launch_configured_domains(Dominit0SystemConfig *system)
{
     for (size_t i = 0; i < system->domain_count; i++) {
          CurrentDomain *current = system->current_domains[i];
          IDomainConfig *config = current->config;
          uint64_t domain_id = UINT64_MAX;
          FacetString domain_name = {0};

          current->platform_state = platform_start_domain(current);
          if (current->platform_state != NULL)
               continue;

          if (config->getdomain_id(config->self, &domain_id) != FACET_OK ||
              config->getdomain_name(config->self, &domain_name) != FACET_OK) {
               klog(LOG_ERROR, "Unable to start configured domain %zu\n", i);
               continue;
          }
          klog(LOG_ERROR, "Unable to start configured domain %llu (%s)\n",
               (unsigned long long)domain_id, domain_name.data);
     }
}

void main(int argc, char **argv, char **envp) {
     platform_init_early();
     klog_init_early(platform_get_early_logging_sink());
     klog(LOG_INFO,"Starting FacetOS...\n");

     #ifdef DEBUG
        klog_dump_debug();
     #endif

     kmalloc_init_early();
     #ifdef DEBUG
        test_kmalloc();
     #endif
     platform_init();

     PlatformConfigSource source;
     PlatformConfigSourceStatus source_status =
          platform_get_config_source(&source);
     if (source_status == PLATFORM_CONFIG_SOURCE_DUPLICATE)
          kpanic("More than one facet.toml boot module was supplied!");
     if (source_status == PLATFORM_CONFIG_SOURCE_INVALID)
          kpanic("Invalid multiboot configuration module information!");

     bool fallback = source_status == PLATFORM_CONFIG_SOURCE_ABSENT;

     FacetConfigDiagnostic diagnostic;
     if (dominit0_config_initialize(source.data, source.size,
                                    !fallback, &diagnostic) != 0) {
          log_config_diagnostic(&diagnostic);
          kpanic("Unable to prepare FacetOS configuration!");
     }

     if (dominit0_logging_initialize(dominit0_config_get_system()) != 0)
          kpanic("Unable to initialise configured logging!");

     if (dominit0_environment_initialize(dominit0_config_get_system()) != 0)
          kpanic("Unable to initialise domain environments!");

     if (fallback)
          klog(LOG_WARN,
               "No facet.toml module found; using built-in development defaults\n");

     log_prepared_configuration(dominit0_config_get_system(), fallback);

     launch_configured_domains(dominit0_config_get_system());

     #ifdef DEBUG
        test_kmalloc();
     #endif

     for (;;) {
        platform_yield();
     }
}
