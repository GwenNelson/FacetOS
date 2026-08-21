#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/interfaces/IPageAllocator.h>

#include <sel4/sel4.h>
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

static vka_object_t debug_endpoint;
static sel4utils_thread_t debug_server_thread;
static seL4_Word next_debug_badge = 1;

static void
debug_putchar(char c)
{
    seL4_DebugPutChar(c);
}

static void
debug_server(seL4_CPtr endpoint)
{
    for (;;) {
        seL4_Word badge = 0;
        seL4_MessageInfo_t info = seL4_Recv(endpoint, &badge);

        if (seL4_MessageInfo_get_length(info) >= 1) {
            debug_putchar((char)seL4_GetMR(0));
        }
    }
}

static void
debug_server_entry(void *endpoint, void *unused, void *ipc_buffer)
{
    (void)unused;
    (void)ipc_buffer;
    debug_server((seL4_CPtr)(uintptr_t)endpoint);
}

static int
setup_debug_server(void)
{
    int error = vka_alloc_endpoint(&sel4_vka, &debug_endpoint);
    if (error != 0) {
        return -1;
    }

    seL4_Word cspace_data = api_make_guard_skip_word(
        seL4_WordBits - simple_get_cnode_size_bits(&sel4_simple));
    sel4utils_thread_config_t config = thread_config_default(
        &sel4_simple,
        simple_get_cnode(&sel4_simple),
        cspace_data,
        seL4_CapNull,
        seL4_MaxPrio);

    error = sel4utils_configure_thread_config(&sel4_vka,
                                              &sel4_vspace,
                                              &sel4_vspace,
                                              config,
                                              &debug_server_thread);
    if (error != 0) {
        vka_free_object(&sel4_vka, &debug_endpoint);
        return -1;
    }

    error = sel4utils_start_thread(&debug_server_thread,
                                   debug_server_entry,
                                   (void *)(uintptr_t)debug_endpoint.cptr,
                                   NULL,
                                   1);
    if (error != 0) {
        sel4utils_clean_up_thread(&sel4_vka, &sel4_vspace,
                                  &debug_server_thread);
        vka_free_object(&sel4_vka, &debug_endpoint);
        return -1;
    }

    return 0;
}

static seL4_CPtr
mint_debug_endpoint(sel4utils_process_t *process)
{
    if (debug_endpoint.cptr == seL4_CapNull) {
        return seL4_CapNull;
    }

    cspacepath_t source;
    vka_cspace_make_path(&sel4_vka, debug_endpoint.cptr, &source);

    seL4_Word badge = next_debug_badge++;
    if (next_debug_badge == 0) {
        next_debug_badge = 1;
    }

    /* The child may send, but cannot receive from the debug endpoint. */
    seL4_CapRights_t send_only =
        seL4_CapRights_new(false, false, false, true);
    return sel4utils_mint_cap_to_process(process, source, send_only, badge);
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

    char *spawn_argv[argc + 1];

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

    char endpoint_string[1][WORD_STRING_SIZE];
    char *endpoint_argv[1];
    sel4utils_create_word_args(endpoint_string, endpoint_argv, 1,
                               (seL4_Word)child_debug_endpoint);

    /* argv[1] is reserved for the child-visible debug endpoint CPtr. */
    spawn_argv[0] = argv[0];
    spawn_argv[1] = endpoint_argv[0];
    for (int i = 1; i < argc; i++) {
        spawn_argv[i + 1] = argv[i];
    }

    if (sel4utils_spawn_process_v(process, &sel4_vka, &sel4_vspace,
                                  argc + 1, spawn_argv, 1) != 0) {
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

     klog(LOG_DEBUG, "platform_init() - setting up debug endpoint\n");
     if (setup_debug_server() != 0)
         kpanic("Unable to set up debug endpoint server!");

     klog(LOG_INFO,"seL4 platform ready\n");
}

void platform_yield(void) {
     seL4_Yield();
}

void platform_debug_print(char* str) {
     seL4_DebugPutString(str);
}
