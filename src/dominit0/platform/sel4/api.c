#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/interfaces/IDebug.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/libfacet/platform/sel4.h>

#include <sel4/sel4.h>
#include <sel4/bootinfo.h>
#include <sel4/arch/bootinfo_types.h>
#include <sel4runtime.h>

#include <simple-default/simple-default.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <elf/elf.h>
#include <sel4utils/api.h>
#include <sel4utils/elf.h>
#include <sel4utils/process.h>
#include <sel4utils/thread.h>
#include <sel4utils/vspace.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "bootinfo.h"

extern seL4_BootInfo* platform_sel4_bi;

#define ALLOCMAN_BOOTSTRAP_POOL_SIZE (80 * 1024 * 1024)

static simple_t sel4_simple;
static allocman_t *sel4_allocman;
static vka_t sel4_vka;

static vspace_t sel4_loader_vspace;
static vspace_t sel4_vspace;

static sel4utils_alloc_data_t sel4_loader_data;
static sel4utils_alloc_data_t sel4_vspace_data;

static IPageAllocator *page_allocator;
static IPageAllocator page_alloc_instance;

static FacetHandle debug_server_handle;
static seL4_Word next_debug_badge = 1;

typedef struct Sel4DomainState {
    sel4utils_process_t process;
} Sel4DomainState;

typedef struct boot_module {
    const void *data;
    size_t size;
    const char *name;
} boot_module_t;

static int
module_name_matches(const char *name, const char *wanted)
{
    const char *base = name;
    const char *end = name;

    while (*end != '\0' && *end != ' ' && *end != '\t') {
        if (*end == '/' || *end == '\\') {
            base = end + 1;
        }
        end++;
    }

    size_t basename_length = (size_t)(end - base);
    return strlen(wanted) == basename_length &&
           memcmp(base, wanted, basename_length) == 0;
}

static int
find_boot_module(const seL4_BootInfo *bi, const char *wanted,
                 boot_module_t *result)
{
    if (bi == NULL || wanted == NULL || result == NULL) {
        return -1;
    }

    const uint8_t *cur =
        (const uint8_t *)bi + seL4_BootInfoFrameSize;
    const uint8_t *end = cur + bi->extraLen;

    bool saw_modules_chunk = false;
    bool found = false;

    while ((size_t)(end - cur) >= sizeof(seL4_BootInfoHeader)) {
        seL4_BootInfoHeader header;
        memcpy(&header, cur, sizeof(header));

        if (header.len < sizeof(header) ||
            header.len > (size_t)(end - cur)) {
            return -1;
        }

        if (header.id == SEL4_BOOTINFO_HEADER_X86_MODULES) {
            if (saw_modules_chunk) {
                return -2;
            }
            saw_modules_chunk = true;
            seL4_X86_BootInfo_modules_t modules;
            size_t descriptor_offset = sizeof(modules);

            if (header.len < sizeof(modules)) {
                return -1;
            }

            memcpy(&modules, cur, sizeof(modules));
            if (modules.module_count >
                (header.len - descriptor_offset) /
                    sizeof(seL4_X86_BootInfo_module_t)) {
                return -1;
            }

            size_t names_offset = descriptor_offset +
                modules.module_count *
                    sizeof(seL4_X86_BootInfo_module_t);

            for (seL4_Word i = 0; i < modules.module_count; i++) {
                seL4_X86_BootInfo_module_t descriptor;
                memcpy(&descriptor,
                       cur + descriptor_offset +
                           i * sizeof(descriptor),
                       sizeof(descriptor));

                if (descriptor.name_offset < names_offset ||
                    descriptor.name_offset >= header.len) {
                    return -1;
                }

                const char *name =
                    (const char *)cur + descriptor.name_offset;
                size_t available = header.len - descriptor.name_offset;
                if (memchr(name, '\0', available) == NULL) {
                    return -1;
                }

                if (module_name_matches(name, wanted)) {
                    if (found) {
                        return -2;
                    }
                    result->data =
                        (const void *)(uintptr_t)descriptor.start;
                    result->size = (size_t)descriptor.size;
                    result->name = name;
                    found = true;
                }
            }
        }

        cur += header.len;
    }

    if (cur != end) {
        return -1;
    }
    return found ? 0 : 1;
}

static void
debug_putchar(char c)
{
    seL4_DebugPutChar(c);
}

static FacetResult
debug_get_interface(void *self, uuid_t iid, FacetHandle *result)
{
    (void)self;
    if (memcmp(iid.bytes, IID_IDebug.bytes, sizeof(iid.bytes)) != 0 &&
        memcmp(iid.bytes, IID_IGenericObject.bytes, sizeof(iid.bytes)) != 0) {
        return FACET_NO_INTERFACE;
    }

    /* IDebug is served by this endpoint already.  Returning the exported
     * handle directly avoids allocating a second server-side cap for this
     * same-object interface query.  The platform reply path treats this as a
     * borrowed handle and does not release the export handle itself. */
    *result = debug_server_handle;
    return FACET_OK;
}

static FacetResult
debug_putc(void *self, uint8_t c)
{
    (void)self;
    debug_putchar((char)c);
    return FACET_OK;
}

static IDebug debug_object = {
    .self = &debug_object,
    .priv = NULL,
    .getInterface = debug_get_interface,
    .putc = debug_putc,
};

static seL4_CPtr
mint_debug_endpoint(sel4utils_process_t *process)
{
    seL4_CPtr debug_endpoint;
    if (facet_sel4_handle_get_cap(debug_server_handle,
                                  &debug_endpoint) != FACET_OK) {
        return seL4_CapNull;
    }

    cspacepath_t source;
    vka_cspace_make_path(&sel4_vka, debug_endpoint, &source);

    seL4_Word badge = next_debug_badge++;
    if (next_debug_badge == 0) {
        next_debug_badge = 1;
    }

    /* The child may call and receive the corresponding reply, but cannot
     * receive requests directly from the debug endpoint. */
    seL4_CapRights_t call_only =
        seL4_CapRights_new(true, true, false, true);
    return sel4utils_mint_cap_to_process(process, source, call_only, badge);
}

static int
copy_elf_program_headers(sel4utils_process_t *process, const elf_t *elf)
{
    process->num_elf_phdrs = sel4utils_elf_num_phdrs(elf);
    process->elf_phdrs = calloc(process->num_elf_phdrs,
                                sizeof(*process->elf_phdrs));
    if (process->elf_phdrs == NULL) {
        return -1;
    }

    sel4utils_elf_read_phdrs(elf, process->num_elf_phdrs,
                             process->elf_phdrs);

    /* Match sel4utils_configure_process_custom()'s musl workaround. */
    for (int i = 0; i < process->num_elf_phdrs; i++) {
        if (process->elf_phdrs[i].p_type == PT_PHDR) {
            process->elf_phdrs[i].p_type = PT_NULL;
        }
    }

    return 0;
}

int
load_and_start_domain(sel4utils_process_t *process,
                      const void *elf_buffer,
                      size_t elf_size,
                      uint8_t priority,
                      int argc,
                      char *argv[])
{
    if (process == NULL || elf_buffer == NULL || elf_size == 0 ||
        argc < 1 || argv == NULL) {
        klog(LOG_ERROR, "load_and_start_domain(): invalid argument\n");
        return -1;
    }

    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL) {
            klog(LOG_ERROR, "load_and_start_domain(): invalid argv\n");
            return -1;
        }
    }

    char *spawn_argv[argc + 4];

    if (sel4_allocman == NULL) {
        klog(LOG_ERROR,
             "load_and_start_domain(): platform_init() has not completed\n");
        return -1;
    }

    elf_t elf;
    if (elf_newFile(elf_buffer, elf_size, &elf) < 0) {
        klog(LOG_ERROR, "load_and_start_domain(): invalid ELF image\n");
        return -1;
    }

    uintptr_t sysinfo = sel4utils_elf_get_vsyscall(&elf);
    sel4utils_process_config_t config =
        process_config_default_simple(&sel4_simple, "<ELF buffer>", priority);

    /* Bypass the CPIO-only ELF path; the image is loaded below. */
    config = process_config_noelf(config,
                                  (void *)elf_getEntryPoint(&elf),
                                  sysinfo);

    if (sel4utils_configure_process_custom(process, &sel4_vka,
                                           &sel4_vspace, config) != 0) {
        klog(LOG_ERROR, "load_and_start_domain(): process setup failed\n");
        return -1;
    }

    seL4_CPtr child_debug_endpoint = mint_debug_endpoint(process);
    if (child_debug_endpoint == seL4_CapNull) {
        klog(LOG_ERROR,
             "load_and_start_domain(): could not mint debug endpoint\n");
        goto fail;
    }

    process->entry_point = sel4utils_elf_load(&process->vspace,
                                              &sel4_vspace,
                                              &sel4_vka,
                                              &sel4_vka,
                                              &elf);
    if (process->entry_point == NULL) {
        klog(LOG_ERROR, "load_and_start_domain(): ELF load failed\n");
        goto fail;
    }

    if (copy_elf_program_headers(process, &elf) != 0) {
        klog(LOG_ERROR,
             "load_and_start_domain(): could not copy ELF program headers\n");
        goto fail;
    }

    char endpoint_string[4][WORD_STRING_SIZE];
    char *endpoint_argv[4];
    seL4_CPtr child_receive_slot = process->cspace_next_free;
    seL4_Word child_words[4] = {
        (seL4_Word)child_debug_endpoint,
        (seL4_Word)SEL4UTILS_CNODE_SLOT,
        (seL4_Word)child_receive_slot,
        (seL4_Word)seL4_WordBits,
    };
    sel4utils_create_word_args(endpoint_string, endpoint_argv, 4,
                               child_words[0], child_words[1],
                               child_words[2], child_words[3]);

    /* argv[1] is the debug endpoint. argv[2..4] describe the empty
     * capability receive slot in the child's CSpace. */
    spawn_argv[0] = argv[0];
    spawn_argv[1] = endpoint_argv[0];
    spawn_argv[2] = endpoint_argv[1];
    spawn_argv[3] = endpoint_argv[2];
    spawn_argv[4] = endpoint_argv[3];
    for (int i = 1; i < argc; i++) {
        spawn_argv[i + 4] = argv[i];
    }

    if (sel4utils_spawn_process_v(process, &sel4_vka, &sel4_vspace,
                                  argc + 4, spawn_argv, 1) != 0) {
        klog(LOG_ERROR, "load_and_start_domain(): process start failed\n");
        goto fail;
    }

    return 0;

fail:
    sel4utils_destroy_process(process, &sel4_vka);
    return -1;
}

size_t sel4_page_alloc_get_page_size(void* self) {
	// temporary hack
	return 4096;
}

static int
sel4_page_alloc_alloc(void *self, size_t count, void **pages)
{
    IPageAllocator *allocator = self;
    vspace_t *vspace = allocator->priv;

    void *base = vspace_new_pages(
        &sel4_vspace,
        seL4_AllRights,
        count,
        seL4_PageBits
    );

    if (base == NULL)
        return -1;

    *pages = base;
    return 0;
}

static int
sel4_page_alloc_free(void *self, size_t count, void *base)
{
    IPageAllocator *allocator = self;
    vspace_t *vspace = allocator->priv;

    if (base == NULL || count == 0)
        return -1;

    vspace_unmap_pages(
        vspace,
        base,
        count,
        seL4_PageBits,
        VSPACE_FREE
    );

    return 0;
}

IPageAllocator* sel4_page_allocator_create(vka_t *vka) {
	klog(LOG_DEBUG,"sel4_page_allocator_create()\n");
	IPageAllocator *retval = &page_alloc_instance;
	retval->self = (void*)retval;
	retval->priv = (void*)&sel4_vspace;
	retval->get_page_size = &sel4_page_alloc_get_page_size;
	retval->alloc         = &sel4_page_alloc_alloc;
	retval->free          = &sel4_page_alloc_free;
	return retval;
}

void platform_init_early(void) {
}

void* allocman_pool;
void platform_init(void) {
     klog(LOG_INFO,"platform_init() for seL4\n");
     platform_sel4_bi = platform_sel4_get_bootinfo();
     #ifdef DEBUG
     platform_sel4_bootinfo_dump(platform_sel4_bi);
     #endif

     klog(LOG_DEBUG, "platform_init() - setting up simple....\n");

     klog(LOG_DEBUG,"platform_init() - allocating %zu for allocman_pool\n", ALLOCMAN_BOOTSTRAP_POOL_SIZE);
     simple_default_init_bootinfo(&sel4_simple, platform_sel4_bi);

     allocman_pool = kmalloc(ALLOCMAN_BOOTSTRAP_POOL_SIZE);
     klog(LOG_DEBUG,"platform_init() - allocated pool at %p\n",allocman_pool);     

     if(allocman_pool==NULL) kpanic("Unable to allocate allocman bootstrap pool!");

     klog(LOG_DEBUG,"platform_init() - setting up allocman\n");
     sel4_allocman = bootstrap_use_current_simple(
		&sel4_simple,
		ALLOCMAN_BOOTSTRAP_POOL_SIZE,
		allocman_pool);

     if (sel4_allocman == NULL) kpanic("Unable to bootstrap seL4 allocman!");

     klog(LOG_DEBUG,"platform_init() - setting up VKA\n");
     allocman_make_vka(&sel4_vka, sel4_allocman);

     klog(LOG_DEBUG,"platform_init() - setting up vSpace for dominit0\n");

     int error;

     error = sel4utils_bootstrap_vspace_with_bootinfo_leaky(
        &sel4_vspace,
        &sel4_vspace_data,
        seL4_CapInitThreadVSpace,
        &sel4_vka,
        platform_sel4_bi);

     if (error) kpanic("Unable to bootstrap dominit0 VSpace");

     klog(LOG_DEBUG,"platform_init() - setting up IPageAllocator\n");
     page_allocator = sel4_page_allocator_create(&sel4_vka);
     if(page_allocator == NULL) kpanic("Unable to create page allocator instance!");

     kmalloc_init(page_allocator);

     klog(LOG_DEBUG, "platform_init() - setting up libfacet debug server\n");
     FacetSel4PlatformConfig facet_config = {
         .vka = &sel4_vka,
         .vspace = &sel4_vspace,
         .simple = &sel4_simple,
     };
     if (facet_sel4_platform_init(&facet_config) != FACET_OK)
         kpanic("Unable to initialise libfacet seL4 platform!");

     if (libfacet_export_interface(&debug_object, &IDebug_MetaData,
                                   &debug_server_handle) != FACET_OK)
         kpanic("Unable to export libfacet debug interface!");

     klog(LOG_INFO,"seL4 platform ready\n");
}

PlatformConfigSourceStatus
platform_get_config_source(PlatformConfigSource *source)
{
    if (source == NULL || platform_sel4_bi == NULL)
        return PLATFORM_CONFIG_SOURCE_INVALID;
    source->data = NULL;
    source->size = 0;
    boot_module_t module;
    int result = find_boot_module(platform_sel4_bi, "facet.toml", &module);
    if (result == 1)
        return PLATFORM_CONFIG_SOURCE_ABSENT;
    if (result == -2)
        return PLATFORM_CONFIG_SOURCE_DUPLICATE;
    if (result != 0)
        return PLATFORM_CONFIG_SOURCE_INVALID;
    source->data = module.data;
    source->size = module.size;
    return PLATFORM_CONFIG_SOURCE_FOUND;
}

void *platform_start_domain(IDomainConfig *config)
{
     if (config == NULL) {
         klog(LOG_ERROR, "platform_start_domain(): invalid domain config\n");
         return NULL;
     }

     Sel4DomainState *state = calloc(1, sizeof(*state));
     if (state == NULL) {
         klog(LOG_ERROR, "platform_start_domain(): out of memory\n");
         return NULL;
     }

     uint64_t domain_id = UINT64_MAX;
     FacetString domain_name = {0};
     if (config->getdomain_id(config->self, &domain_id) != FACET_OK ||
         config->getdomain_name(config->self, &domain_name) != FACET_OK) {
         klog(LOG_ERROR, "platform_start_domain(): invalid domain config\n");
         free(state);
         return NULL;
     }

     klog(LOG_DEBUG,
          "platform_start_domain(): locating dominit module for domain %llu (%s)\n",
          (unsigned long long)domain_id, domain_name.data);
     boot_module_t dominit_module;
     if (find_boot_module(platform_sel4_bi, "dominit", &dominit_module) != 0) {
         klog(LOG_ERROR,
              "platform_start_domain(): unable to find unique dominit boot module\n");
         free(state);
         return NULL;
     }

     klog(LOG_INFO,
          "Starting dominit for domain %llu (%s) from %s at %p (%zu bytes)\n",
          (unsigned long long)domain_id, domain_name.data, dominit_module.name,
          dominit_module.data, dominit_module.size);

     char *dominit_argv[] = { "dominit" };
     if (load_and_start_domain(&state->process,
                               dominit_module.data,
                               dominit_module.size,
                               seL4_MaxPrio,
                               1,
                               dominit_argv) != 0) {
         klog(LOG_ERROR,
              "platform_start_domain(): unable to start dominit for domain %llu (%s)\n",
              (unsigned long long)domain_id, domain_name.data);
         free(state);
         return NULL;
     }

     return state;
}

void platform_yield(void) {
     seL4_Yield();
}

void platform_debug_print(char* str) {
     seL4_DebugPutString(str);
}
