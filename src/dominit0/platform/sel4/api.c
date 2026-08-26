#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/kmalloc.h>
#include <facetos/dominit0/kpanic.h>
#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/terminal.h>
#include <facetos/interfaces/IPageAllocator.h>
#include <facetos/libc.h>
#include <facetos/libfacet/platform/sel4.h>

#include <sel4/sel4.h>
#include <sel4/bootinfo.h>
#include <sel4/arch/bootinfo_types.h>
#include <sel4runtime.h>

#include <simple-default/simple-default.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vka/ipcbuffer.h>
#include <vspace/vspace.h>
#include <elf/elf.h>
#include <sel4utils/api.h>
#include <sel4utils/elf.h>
#include <sel4utils/process.h>
#include <sel4utils/thread.h>
#include <sel4utils/vspace.h>
#include <sel4utils/helpers.h>
#include <sel4utils/util.h>
#include <sel4platsupport/device.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootinfo.h"

extern seL4_BootInfo* platform_sel4_bi;

/* Keep enough dynamic heap after platform setup for the intentionally
 * capability-rich dominit0 service graph (terminal, config, auth, sessions).
 * The bootstrap allocman itself does not require the former 80 MiB pool. */
#define ALLOCMAN_BOOTSTRAP_POOL_SIZE (60 * 1024 * 1024)
#define SEAT_EXPORT_SLOT_COUNT 40u
#define SEAT_VGA_ADDRESS UINT64_C(0x7000000000)

static simple_t sel4_simple;
static allocman_t *sel4_allocman;
static vka_t sel4_vka;
static vspace_t sel4_loader_vspace;
static vspace_t sel4_vspace;

static sel4utils_alloc_data_t sel4_loader_data;
static sel4utils_alloc_data_t sel4_vspace_data;

static IPageAllocator *page_allocator;
static IPageAllocator page_alloc_instance;

static seL4_Word next_debug_badge = 1;

typedef struct Sel4ChildPageAllocation {
    uintptr_t base;
    uint64_t page_count;
    struct Sel4ChildPageAllocation *next;
} Sel4ChildPageAllocation;

typedef struct Sel4ChildPageAllocator {
    IPageAllocator interface;
    sel4utils_process_t *process;
    FacetHandle exported_handle;
    Sel4ChildPageAllocation *allocations;
} Sel4ChildPageAllocator;

typedef struct Sel4DomainState {
    sel4utils_process_t process;
    Sel4ChildPageAllocator page_allocator;
    Dominit0DomainEnvironment *environment;
} Sel4DomainState;

typedef struct Sel4ProgramState {
    sel4utils_process_t process;
    Sel4ChildPageAllocator page_allocator;
    Dominit0ProcessEnvironment *environment;
} Sel4ProgramState;

typedef struct Sel4SeatState {
    sel4utils_process_t process;
    vka_object_t service_endpoint;
    vka_object_t ready_endpoint;
    vka_object_t vga_frame;
    FacetHandle bootstrap_handle;
    cspacepath_t ready_receive_path;
    bool ready_receive_allocated;
    cspacepath_t fault_endpoint_path;
    bool fault_endpoint_allocated;
    FacetAuxvEntry auxv[9];
    size_t auxc;
    CurrentSeat *seat;
} Sel4SeatState;

static bool
iid_equal(uuid_t left, uuid_t right)
{
    return memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

static bool
handle_is_bound(FacetHandle handle)
{
    return handle.platform != NULL;
}

static FacetResult
child_page_allocator_get_interface(void *self, uuid_t iid,
                                   FacetHandle *result)
{
    Sel4ChildPageAllocator *allocator = self;
    if (result == NULL)
        return FACET_INVALID_ARGUMENT;
    *result = (FacetHandle){0};
    if (!iid_equal(iid, IID_IGenericObject) &&
        !iid_equal(iid, IID_IPageAllocator))
        return FACET_NO_INTERFACE;
    if (!handle_is_bound(allocator->exported_handle))
        return FACET_INVALID_HANDLE;
    *result = allocator->exported_handle;
    return FACET_OK;
}

static FacetResult
child_page_allocator_get_page_size(void *self, uint64_t *page_size)
{
    (void)self;
    if (page_size == NULL)
        return FACET_INVALID_ARGUMENT;
    *page_size = UINT64_C(1) << seL4_PageBits;
    return FACET_OK;
}

static FacetResult
child_page_allocator_alloc(void *self, uint64_t count, void **pages)
{
    Sel4ChildPageAllocator *allocator = self;
    if (pages == NULL)
        return FACET_INVALID_ARGUMENT;
    *pages = NULL;
    if (allocator->process == NULL || count == 0 || count > SIZE_MAX)
        return FACET_INVALID_ARGUMENT;

    Sel4ChildPageAllocation *record = calloc(1, sizeof(*record));
    if (record == NULL)
        return FACET_OUT_OF_MEMORY;

    void *base = vspace_new_pages(&allocator->process->vspace,
                                  seL4_AllRights, (size_t)count,
                                  seL4_PageBits);
    if (base == NULL) {
        free(record);
        return FACET_OUT_OF_MEMORY;
    }

    record->base = (uintptr_t)base;
    record->page_count = count;
    record->next = allocator->allocations;
    allocator->allocations = record;
    *pages = base;
    return FACET_OK;
}

static FacetResult
child_page_allocator_free(void *self, uint64_t count, uint64_t base)
{
    Sel4ChildPageAllocator *allocator = self;
    if (allocator->process == NULL || count == 0 || base == 0 ||
        base > UINTPTR_MAX)
        return FACET_INVALID_ARGUMENT;

    Sel4ChildPageAllocation **link = &allocator->allocations;
    while (*link != NULL && ((*link)->base != (uintptr_t)base ||
                             (*link)->page_count != count)) {
        link = &(*link)->next;
    }
    if (*link == NULL)
        return FACET_INVALID_ARGUMENT;

    Sel4ChildPageAllocation *record = *link;
    vspace_unmap_pages(&allocator->process->vspace, (void *)(uintptr_t)base,
                       (size_t)count, seL4_PageBits, VSPACE_FREE);
    *link = record->next;
    free(record);
    return FACET_OK;
}

static void
child_page_allocator_destroy(Sel4ChildPageAllocator *allocator)
{
    while (allocator->allocations != NULL) {
        Sel4ChildPageAllocation *record = allocator->allocations;
        allocator->allocations = record->next;
        if (allocator->process != NULL) {
            vspace_unmap_pages(&allocator->process->vspace,
                               (void *)record->base,
                               (size_t)record->page_count, seL4_PageBits,
                               VSPACE_FREE);
        }
        free(record);
    }
    if (handle_is_bound(allocator->exported_handle))
        (void)libfacet_unexport_interface(allocator->exported_handle);
    memset(allocator, 0, sizeof(*allocator));
}

static int
child_page_allocator_self_test(Sel4ChildPageAllocator *allocator)
{
    uint64_t page_size = 0;
    void *page = NULL;
    if (allocator->interface.get_page_size(allocator, &page_size) != FACET_OK ||
        page_size != (UINT64_C(1) << seL4_PageBits) ||
        allocator->interface.alloc(allocator, 0, &page) == FACET_OK ||
        allocator->interface.alloc(allocator, 1, &page) != FACET_OK ||
        page == NULL ||
        allocator->interface.free(allocator, 1, (uint64_t)(uintptr_t)page) !=
            FACET_OK ||
        allocator->interface.free(allocator, 1, (uint64_t)(uintptr_t)page) ==
            FACET_OK)
        return -1;
    return 0;
}

static int
child_page_allocator_prepare(sel4utils_process_t *process, void *context,
                             FacetHandle *out_bootstrap_handle,
                             FacetAuxvEntry extra_auxv[], size_t *extra_auxc)
{
    Sel4DomainState *state = context;
    if (process == NULL || state == NULL || out_bootstrap_handle == NULL ||
        process != &state->process || extra_auxv == NULL || extra_auxc == NULL)
        return -1;
    *extra_auxc = 0;
    *out_bootstrap_handle = (FacetHandle){0};

    Sel4ChildPageAllocator *allocator = &state->page_allocator;
    allocator->process = process;
    allocator->interface.self = allocator;
    allocator->interface.priv = allocator;
    allocator->interface.getInterface = child_page_allocator_get_interface;
    allocator->interface.get_page_size = child_page_allocator_get_page_size;
    allocator->interface.alloc = child_page_allocator_alloc;
    allocator->interface.free = child_page_allocator_free;

    if (libfacet_export_interface(&allocator->interface,
                                  &IPageAllocator_MetaData,
                                  &allocator->exported_handle) != FACET_OK) {
        child_page_allocator_destroy(allocator);
        return -1;
    }
    if (child_page_allocator_self_test(allocator) != 0) {
        child_page_allocator_destroy(allocator);
        return -1;
    }
    if (dominit0_environment_bind_page_allocator(
            state->environment, allocator->exported_handle) != 0) {
        child_page_allocator_destroy(allocator);
        return -1;
    }
    *out_bootstrap_handle =
        dominit0_environment_root_handle(state->environment);
    if (!handle_is_bound(*out_bootstrap_handle)) {
        child_page_allocator_destroy(allocator);
        return -1;
    }
    return 0;
}

static void
child_page_allocator_cleanup(void *context)
{
    if (context != NULL)
        child_page_allocator_destroy(&((Sel4DomainState *)context)->page_allocator);
}

static int
program_page_allocator_prepare(sel4utils_process_t *process, void *context,
                               FacetHandle *out_bootstrap_handle,
                               FacetAuxvEntry extra_auxv[], size_t *extra_auxc)
{
    Sel4ProgramState *state = context;
    if (process == NULL || state == NULL || out_bootstrap_handle == NULL ||
        process != &state->process || state->environment == NULL ||
        extra_auxv == NULL || extra_auxc == NULL)
        return -1;
    *extra_auxc = 0;
    *out_bootstrap_handle = (FacetHandle){0};
    Sel4ChildPageAllocator *allocator = &state->page_allocator;
    allocator->process = process;
    allocator->interface.self = allocator;
    allocator->interface.priv = allocator;
    allocator->interface.getInterface = child_page_allocator_get_interface;
    allocator->interface.get_page_size = child_page_allocator_get_page_size;
    allocator->interface.alloc = child_page_allocator_alloc;
    allocator->interface.free = child_page_allocator_free;
    if (libfacet_export_interface(&allocator->interface,
                                  &IPageAllocator_MetaData,
                                  &allocator->exported_handle) != FACET_OK ||
        child_page_allocator_self_test(allocator) != 0 ||
        dominit0_process_environment_bind_page_allocator(
            state->environment, allocator->exported_handle) != 0) {
        child_page_allocator_destroy(allocator);
        return -1;
    }
    *out_bootstrap_handle =
        dominit0_process_environment_root_handle(state->environment);
    return handle_is_bound(*out_bootstrap_handle) ? 0 : -1;
}

static void program_page_allocator_cleanup(void *context)
{
    if (context != NULL)
        child_page_allocator_destroy(&((Sel4ProgramState *)context)->page_allocator);
}

static int seat_copy_ioport(sel4utils_process_t *process, uint16_t first,
                            uint16_t last, seL4_CPtr *child_cap)
{
    cspacepath_t path;
    if (vka_cspace_alloc_path(&sel4_vka, &path) != 0) return -1;
    if (simple_get_IOPort_cap(&sel4_simple, first, last, path.root,
                              path.capPtr, path.capDepth) != seL4_NoError) {
        vka_cspace_free_path(&sel4_vka, path);
        return -1;
    }
    *child_cap = sel4utils_copy_cap_to_process(process, &sel4_vka,
                                               path.capPtr);
    vka_cnode_delete(&path);
    vka_cspace_free_path(&sel4_vka, path);
    return *child_cap == seL4_CapNull ? -1 : 0;
}

static int seat_map_vga(Sel4SeatState *state, sel4utils_process_t *process)
{
    if (sel4platsupport_alloc_frame_at(&sel4_vka, 0xb8000,
                                       seL4_PageBits,
                                       &state->vga_frame) != seL4_NoError)
        return -1;
    void *address = (void *)(uintptr_t)SEAT_VGA_ADDRESS;
    reservation_t reservation = vspace_reserve_range_at(
        &process->vspace, address, BIT(seL4_PageBits), seL4_AllRights, 0);
    seL4_CPtr cap = state->vga_frame.cptr;
    uintptr_t cookie = state->vga_frame.ut;
    int result = reservation.res == NULL ? -1 : vspace_map_pages_at_vaddr(
        &process->vspace, &cap, &cookie, address, 1, seL4_PageBits,
        reservation);
    return result;
}

static int seat_prepare(sel4utils_process_t *process, void *context,
                        FacetHandle *out_bootstrap_handle,
                        FacetAuxvEntry extra_auxv[], size_t *extra_auxc)
{
    Sel4SeatState *state = context;
    if (process == NULL || state == NULL || state->seat == NULL ||
        out_bootstrap_handle == NULL || extra_auxv == NULL ||
        extra_auxc == NULL)
        return -1;
    if (vka_alloc_endpoint(&sel4_vka, &state->service_endpoint) != 0 ||
        state->ready_endpoint.cptr == seL4_CapNull)
        return -1;
    seL4_CPtr child_service = sel4utils_copy_cap_to_process(
        process, &sel4_vka, state->service_endpoint.cptr);
    cspacepath_t ready_source;
    vka_cspace_make_path(&sel4_vka, state->ready_endpoint.cptr,
                         &ready_source);
    seL4_CPtr child_ready = sel4utils_mint_cap_to_process(
        process, ready_source,
        seL4_CapRights_new(true, true, false, true), 1);
    if (child_service == seL4_CapNull || child_ready == seL4_CapNull)
        return -1;

    seL4_CPtr device0 = seL4_CapNull;
    seL4_CPtr device1 = seL4_CapNull;
    uint64_t vga_address = 0;
    if (state->seat->config->type == FACET_CONFIG_SEAT_SERIAL) {
        if (seat_copy_ioport(process, 0x3f8, 0x3ff, &device0) != 0)
            return -1;
    } else {
        if (seat_copy_ioport(process, 0x60, 0x64, &device0) != 0 ||
            seat_copy_ioport(process, 0x3d4, 0x3d5, &device1) != 0 ||
            seat_map_vga(state, process) != 0)
            return -1;
        vga_address = SEAT_VGA_ADDRESS;
    }

    seL4_CPtr receive_slot = process->cspace_next_free++;
    seL4_CPtr export_slot = process->cspace_next_free;
    process->cspace_next_free += SEAT_EXPORT_SLOT_COUNT;
    FacetAuxvEntry seat_entries[] = {
        {AT_FACET_SEAT_SERVICE_ENDPOINT, child_service},
        {AT_FACET_SEAT_READY_ENDPOINT, child_ready},
        {AT_FACET_SEAT_DEVICE0, device0},
        {AT_FACET_SEAT_DEVICE1, device1},
        {AT_FACET_SEAT_RECEIVE_SLOT, receive_slot},
        {AT_FACET_SEAT_EXPORT_SLOT, export_slot},
        {AT_FACET_SEAT_CNODE, SEL4UTILS_CNODE_SLOT},
        {AT_FACET_SEAT_DEPTH, seL4_WordBits},
        {AT_FACET_SEAT_VGA_ADDRESS, vga_address},
    };
    memcpy(extra_auxv, seat_entries, sizeof(seat_entries));
    *extra_auxc = sizeof(seat_entries) / sizeof(seat_entries[0]);

    if (vka_cspace_alloc_path(&sel4_vka, &state->ready_receive_path) != 0)
        return -1;
    state->ready_receive_allocated = true;
    if (facet_sel4_handle_from_cap(state->ready_endpoint.cptr,
                                   &state->bootstrap_handle) != FACET_OK)
        return -1;
    *out_bootstrap_handle = state->bootstrap_handle;
    return 0;
}

static void seat_cleanup(void *context)
{
    Sel4SeatState *state = context;
    if (state == NULL) return;
    if (state->bootstrap_handle.platform != NULL) {
        (void)libfacet_handle_release(state->bootstrap_handle);
        state->bootstrap_handle = (FacetHandle){0};
    }
    if (state->ready_receive_allocated) {
        vka_cnode_delete(&state->ready_receive_path);
        vka_cspace_free_path(&sel4_vka, state->ready_receive_path);
        state->ready_receive_allocated = false;
    }
    if (state->fault_endpoint_allocated) {
        vka_cnode_delete(&state->fault_endpoint_path);
        vka_cspace_free_path(&sel4_vka, state->fault_endpoint_path);
        state->fault_endpoint_allocated = false;
    }
    if (state->service_endpoint.cptr != seL4_CapNull) {
        vka_free_object(&sel4_vka, &state->service_endpoint);
        state->service_endpoint.cptr = seL4_CapNull;
    }
    if (state->ready_endpoint.cptr != seL4_CapNull) {
        vka_free_object(&sel4_vka, &state->ready_endpoint);
        state->ready_endpoint.cptr = seL4_CapNull;
    }
    if (state->vga_frame.cptr != seL4_CapNull) {
        vka_free_object(&sel4_vka, &state->vga_frame);
        state->vga_frame.cptr = seL4_CapNull;
    }
}

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

static seL4_CPtr
mint_endpoint_to_process(sel4utils_process_t *process, FacetHandle handle)
{
    seL4_CPtr endpoint;
    if (facet_sel4_handle_get_cap(handle, &endpoint) != FACET_OK) {
        return seL4_CapNull;
    }

    cspacepath_t source;
    vka_cspace_make_path(&sel4_vka, endpoint, &source);

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

typedef int (*DomainPreSpawnHook)(sel4utils_process_t *process,
                                  void *context,
                                  FacetHandle *out_bootstrap_handle,
                                  FacetAuxvEntry extra_auxv[],
                                  size_t *extra_auxc);
typedef void (*DomainFailureCleanup)(void *context);

/* libsel4utils exports this primitive but omits it from its public headers. */
extern int sel4utils_stack_write(vspace_t *current_vspace,
                                 vspace_t *target_vspace, vka_t *vka,
                                 void *buffer, size_t length,
                                 uintptr_t *stack_pointer);

static int
stack_push(const void *value, size_t size, uintptr_t *stack_pointer,
           sel4utils_process_t *process)
{
    return sel4utils_stack_write(&sel4_vspace, &process->vspace, &sel4_vka,
                                 (void *)value, size, stack_pointer);
}

static int
spawn_process_with_auxv(sel4utils_process_t *process, int argc, char *argv[],
                        size_t envc, const char *const envp[],
                        size_t facet_auxc,
                        const FacetAuxvEntry facet_auxv[])
{
    if (process == NULL || argc < 1 || argv == NULL ||
        (envc != 0 && envp == NULL) ||
        facet_auxv_validate(facet_auxc, facet_auxv) != 0)
        return -1;

    uintptr_t *child_argv = calloc((size_t)argc, sizeof(*child_argv));
    uintptr_t *child_envp = calloc(envc == 0 ? 1 : envc,
                                  sizeof(*child_envp));
    size_t standard_auxc = process->sysinfo == 0 ? 6 : 7;
    size_t auxc = standard_auxc + facet_auxc;
    Elf_auxv_t *auxv = calloc(auxc, sizeof(*auxv));
    if (child_argv == NULL || child_envp == NULL || auxv == NULL)
        goto fail;

    uintptr_t stack_pointer =
        (uintptr_t)process->thread.stack_top - sizeof(seL4_Word);
    uintptr_t phdr_address;
    if (stack_push(process->elf_phdrs,
                   process->num_elf_phdrs * sizeof(Elf_Phdr),
                   &stack_pointer, process) != 0)
        goto fail;
    phdr_address = stack_pointer;

    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL ||
            stack_push(argv[i], strlen(argv[i]) + 1, &stack_pointer,
                       process) != 0)
            goto fail;
        child_argv[i] = stack_pointer;
        stack_pointer = ROUND_DOWN(stack_pointer, 4);
    }
    for (size_t i = 0; i < envc; i++) {
        if (envp[i] == NULL ||
            stack_push(envp[i], strlen(envp[i]) + 1, &stack_pointer,
                       process) != 0)
            goto fail;
        child_envp[i] = stack_pointer;
        stack_pointer = ROUND_DOWN(stack_pointer, 4);
    }

    size_t next = 0;
#define ADD_AUXV(key, val) do { \
    auxv[next].a_type = (key); \
    auxv[next++].a_un.a_val = (long)(val); \
} while (0)
    ADD_AUXV(AT_PAGESZ, process->pagesz);
    ADD_AUXV(AT_PHDR, phdr_address);
    ADD_AUXV(AT_PHNUM, process->num_elf_phdrs);
    ADD_AUXV(AT_PHENT, sizeof(Elf_Phdr));
    ADD_AUXV(AT_SEL4_IPC_BUFFER_PTR, process->thread.ipc_buffer_addr);
    ADD_AUXV(AT_SEL4_TCB, process->dest_tcb_cptr);
    if (process->sysinfo != 0)
        ADD_AUXV(AT_SYSINFO, process->sysinfo);
    for (size_t i = 0; i < facet_auxc; i++)
        ADD_AUXV(facet_auxv[i].type, facet_auxv[i].value);
#undef ADD_AUXV

    size_t frame_size = sizeof(Elf_auxv_t) * (auxc + 1) +
                        sizeof(uintptr_t) * (envc + 1) +
                        sizeof(uintptr_t) * ((size_t)argc + 1) +
                        sizeof(seL4_Word);
    uintptr_t final_pointer = stack_pointer - frame_size;
    stack_pointer -= final_pointer -
                     ALIGN_DOWN(final_pointer, STACK_CALL_ALIGNMENT);

    Elf_auxv_t terminator = {.a_type = AT_NULL, .a_un.a_val = 0};
    uintptr_t zero = 0;
    seL4_Word argument_count = (seL4_Word)argc;
    if (stack_push(&terminator, sizeof(terminator), &stack_pointer,
                   process) != 0 ||
        stack_push(auxv, sizeof(*auxv) * auxc, &stack_pointer, process) != 0 ||
        stack_push(&zero, sizeof(zero), &stack_pointer, process) != 0 ||
        (envc != 0 && stack_push(child_envp, sizeof(*child_envp) * envc,
                                 &stack_pointer, process) != 0) ||
        stack_push(&zero, sizeof(zero), &stack_pointer, process) != 0 ||
        stack_push(child_argv, sizeof(*child_argv) * (size_t)argc,
                   &stack_pointer, process) != 0 ||
        stack_push(&argument_count, sizeof(argument_count), &stack_pointer,
                   process) != 0)
        goto fail;

    seL4_UserContext context = {0};
    if (sel4utils_arch_init_context(process->entry_point,
                                    (void *)stack_pointer, &context) != 0)
        goto fail;
    process->thread.initial_stack_pointer = (void *)stack_pointer;
    int result = seL4_TCB_WriteRegisters(
        process->thread.tcb.cptr, 1, 0,
        sizeof(context) / sizeof(seL4_Word), &context);
    free(auxv);
    free(child_envp);
    free(child_argv);
    return result;

fail:
    free(auxv);
    free(child_envp);
    free(child_argv);
    return -1;
}

static int
load_and_start_domain(sel4utils_process_t *process,
                      const void *elf_buffer,
                      size_t elf_size,
                      uint8_t priority,
                      int argc,
                      char *argv[],
                      size_t envc,
                      const char *const envp[],
                      DomainPreSpawnHook pre_spawn,
                      DomainFailureCleanup failure_cleanup,
                      void *hook_context,
                      seL4_CPtr fault_endpoint)
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
    for (size_t i = 0; i < elf_getNumProgramHeaders(&elf); i++) {
        if (elf_getProgramHeaderType(&elf, i) != PT_INTERP) continue;
        klog(LOG_ERROR,
             "load_and_start_domain(): dynamically linked ELF is unsupported\n");
        return -1;
    }

    uintptr_t sysinfo = sel4utils_elf_get_vsyscall(&elf);
    sel4utils_process_config_t config =
        process_config_default_simple(&sel4_simple, "<ELF buffer>", priority);

    /* Bypass the CPIO-only ELF path; the image is loaded below. */
    config = process_config_noelf(config,
                                  (void *)elf_getEntryPoint(&elf),
                                  sysinfo);
    if (fault_endpoint != seL4_CapNull)
        config = process_config_fault_cptr(config, fault_endpoint);

    if (sel4utils_configure_process_custom(process, &sel4_vka,
                                           &sel4_vspace, config) != 0) {
        klog(LOG_ERROR, "load_and_start_domain(): process setup failed\n");
        return -1;
    }

    FacetHandle child_bootstrap_handle = {0};
    FacetAuxvEntry extra_auxv[16];
    size_t extra_auxc = 0;
    if (pre_spawn != NULL &&
        pre_spawn(process, hook_context, &child_bootstrap_handle,
                  extra_auxv, &extra_auxc) != 0) {
        klog(LOG_ERROR,
             "load_and_start_domain(): process service setup failed\n");
        goto fail;
    }

    if (!handle_is_bound(child_bootstrap_handle)) {
        klog(LOG_ERROR,
             "load_and_start_domain(): process has no bootstrap environment\n");
        goto fail;
    }
    seL4_CPtr child_bootstrap_endpoint =
        mint_endpoint_to_process(process, child_bootstrap_handle);
    if (child_bootstrap_endpoint == seL4_CapNull) {
        klog(LOG_ERROR,
             "load_and_start_domain(): could not mint bootstrap environment\n");
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

    seL4_CPtr child_receive_slot = process->cspace_next_free;
    FacetAuxvEntry facet_auxv[21] = {
        {AT_FACET_ABI_VERSION, FACETOS_STARTUP_ABI_VERSION},
        {AT_FACET_ROOT_OBJECT, (uintptr_t)child_bootstrap_endpoint},
        {AT_FACET_RECEIVE_CNODE, SEL4UTILS_CNODE_SLOT},
        {AT_FACET_RECEIVE_SLOT, (uintptr_t)child_receive_slot},
        {AT_FACET_RECEIVE_DEPTH, seL4_WordBits},
    };
    size_t facet_auxc = 5;
    if (extra_auxc > sizeof(extra_auxv) / sizeof(extra_auxv[0]) ||
        facet_auxc + extra_auxc > sizeof(facet_auxv) / sizeof(facet_auxv[0]))
        goto fail;
    memcpy(&facet_auxv[facet_auxc], extra_auxv,
           extra_auxc * sizeof(extra_auxv[0]));
    facet_auxc += extra_auxc;

    if (spawn_process_with_auxv(process, argc, argv, envc, envp,
                                facet_auxc, facet_auxv) != 0) {
        klog(LOG_ERROR, "load_and_start_domain(): process start failed\n");
        goto fail;
    }

    return 0;

fail:
    if (failure_cleanup != NULL)
        failure_cleanup(hook_context);
    sel4utils_destroy_process(process, &sel4_vka);
    return -1;
}

static FacetResult
sel4_page_alloc_get_interface(void *self, uuid_t iid, FacetHandle *result)
{
    IPageAllocator *allocator = self;
    (void)allocator;
    (void)iid;
    if (result == NULL)
        return FACET_INVALID_ARGUMENT;
    *result = (FacetHandle){0};
    return FACET_NO_INTERFACE;
}

static FacetResult
sel4_page_alloc_get_page_size(void *self, uint64_t *page_size)
{
    (void)self;
    if (page_size == NULL)
        return FACET_INVALID_ARGUMENT;
    *page_size = UINT64_C(1) << seL4_PageBits;
    return FACET_OK;
}

static FacetResult
sel4_page_alloc_alloc(void *self, uint64_t count, void **pages)
{
    IPageAllocator *allocator = self;
    if (pages == NULL || count == 0 || count > SIZE_MAX)
        return FACET_INVALID_ARGUMENT;
    vspace_t *vspace = allocator->priv;

    void *base = vspace_new_pages(
        vspace,
        seL4_AllRights,
        (size_t)count,
        seL4_PageBits
    );

    if (base == NULL)
        return FACET_OUT_OF_MEMORY;

    *pages = base;
    return FACET_OK;
}

static FacetResult
sel4_page_alloc_free(void *self, uint64_t count, uint64_t base)
{
    IPageAllocator *allocator = self;
    vspace_t *vspace = allocator->priv;

    if (base == 0 || count == 0 || count > SIZE_MAX || base > UINTPTR_MAX)
        return FACET_INVALID_ARGUMENT;

    vspace_unmap_pages(
        vspace,
        (void *)(uintptr_t)base,
        (size_t)count,
        seL4_PageBits,
        VSPACE_FREE
    );

    return FACET_OK;
}

IPageAllocator *
sel4_page_allocator_create(vka_t *vka)
{
    (void)vka;
    klog(LOG_DEBUG, "sel4_page_allocator_create()\n");
    IPageAllocator *retval = &page_alloc_instance;
    retval->self = retval;
    retval->priv = &sel4_vspace;
    retval->getInterface = sel4_page_alloc_get_interface;
    retval->get_page_size = sel4_page_alloc_get_page_size;
    retval->alloc = sel4_page_alloc_alloc;
    retval->free = sel4_page_alloc_free;
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

     klog(LOG_DEBUG, "platform_init() - setting up libfacet platform\n");
     FacetSel4PlatformConfig facet_config = {
         .vka = &sel4_vka,
         .vspace = &sel4_vspace,
         .simple = &sel4_simple,
     };
     if (facet_sel4_platform_init(&facet_config) != FACET_OK)
         kpanic("Unable to initialise libfacet seL4 platform!");

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

PlatformConfigSourceStatus
platform_get_boot_module(const char *name, PlatformConfigSource *source)
{
    if (name == NULL || *name == '\0' || source == NULL || platform_sel4_bi == NULL)
        return PLATFORM_CONFIG_SOURCE_INVALID;
    source->data = NULL;
    source->size = 0;
    boot_module_t module;
    int result = find_boot_module(platform_sel4_bi, name, &module);
    if (result == 1) return PLATFORM_CONFIG_SOURCE_ABSENT;
    if (result == -2) return PLATFORM_CONFIG_SOURCE_DUPLICATE;
    if (result != 0) return PLATFORM_CONFIG_SOURCE_INVALID;
    source->data = module.data;
    source->size = module.size;
    return PLATFORM_CONFIG_SOURCE_FOUND;
}

void *platform_start_domain(CurrentDomain *current)
{
     if (current == NULL || current->config == NULL ||
         current->environment == NULL) {
         klog(LOG_ERROR, "platform_start_domain(): invalid domain config\n");
         return NULL;
     }
     IDomainConfig *config = current->config;

     Sel4DomainState *state = calloc(1, sizeof(*state));
     if (state == NULL) {
         klog(LOG_ERROR, "platform_start_domain(): out of memory\n");
         return NULL;
     }
     state->environment = current->environment;

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
                               dominit_argv,
                               0, NULL,
                               child_page_allocator_prepare,
                               child_page_allocator_cleanup,
                               state, seL4_CapNull) != 0) {
         klog(LOG_ERROR,
              "platform_start_domain(): unable to start dominit for domain %llu (%s)\n",
              (unsigned long long)domain_id, domain_name.data);
         free(state);
         return NULL;
     }

     klog(LOG_INFO,
          "Prepared and self-tested child IPageAllocator for domain %llu (%s)\n",
          (unsigned long long)domain_id, domain_name.data);

     return state;
}

void *platform_start_seat(CurrentSeat *seat, const void *elf_data,
                          size_t elf_size, FacetHandle *out_seat)
{
    if (seat == NULL || seat->config == NULL || elf_data == NULL ||
        elf_size == 0 || out_seat == NULL)
        return NULL;
    *out_seat = (FacetHandle){0};
    Sel4SeatState *state = calloc(1, sizeof(*state));
    if (state == NULL) return NULL;
    state->seat = seat;
    cspacepath_t ready_source;
    if (vka_alloc_endpoint(&sel4_vka, &state->ready_endpoint) != 0 ||
        vka_cspace_alloc_path(&sel4_vka, &state->fault_endpoint_path) != 0) {
        seat_cleanup(state);
        free(state);
        return NULL;
    }
    state->fault_endpoint_allocated = true;
    vka_cspace_make_path(&sel4_vka, state->ready_endpoint.cptr,
                         &ready_source);
    if (vka_cnode_mint(&state->fault_endpoint_path, &ready_source,
                       seL4_AllRights, 2) != seL4_NoError) {
        seat_cleanup(state);
        free(state);
        return NULL;
    }
    char *seat_argv[] = {(char *)seat->config->server};
    if (load_and_start_domain(&state->process, elf_data, elf_size,
                              seL4_MaxPrio, 1, seat_argv, 0, NULL,
                              seat_prepare, seat_cleanup, state,
                              state->fault_endpoint_path.capPtr) != 0) {
        seat_cleanup(state);
        free(state);
        return NULL;
    }
    vka_cnode_delete(&state->fault_endpoint_path);
    vka_cspace_free_path(&sel4_vka, state->fault_endpoint_path);
    state->fault_endpoint_allocated = false;

    vka_set_cap_receive_path(&state->ready_receive_path);
    seL4_Word badge = 0;
    seL4_MessageInfo_t ready = seL4_Recv(state->ready_endpoint.cptr, &badge);
    if (badge == 2) {
        klog(LOG_ERROR, "Seat server %s faulted during startup (label %llu)\n",
             seat->config->server,
             (unsigned long long)seL4_MessageInfo_get_label(ready));
        sel4utils_destroy_process(&state->process, &sel4_vka);
        seat_cleanup(state);
        free(state);
        return NULL;
    }
    if (badge != 1 || seL4_MessageInfo_get_extraCaps(ready) != 1 ||
        facet_sel4_handle_from_cap(state->ready_receive_path.capPtr,
                                   out_seat) != FACET_OK) {
        seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));
        sel4utils_destroy_process(&state->process, &sel4_vka);
        seat_cleanup(state);
        free(state);
        return NULL;
    }
    seL4_Reply(seL4_MessageInfo_new(0, 0, 0, 0));
    if (state->bootstrap_handle.platform != NULL) {
        (void)libfacet_handle_release(state->bootstrap_handle);
        state->bootstrap_handle = (FacetHandle){0};
    }
    klog(LOG_INFO, "Started isolated seat server %s for %s\n",
         seat->config->server, seat->config->name);
    return state;
}

void *platform_start_process(CurrentDomain *domain, const void *elf_data,
                             size_t elf_size, int argc, char *argv[],
                             Dominit0ProcessEnvironment *environment)
{
    if (domain == NULL || elf_data == NULL || elf_size == 0 || argc < 1 ||
        argv == NULL || environment == NULL)
        return NULL;
    Sel4ProgramState *state = calloc(1, sizeof(*state));
    if (state == NULL) return NULL;
    state->environment = environment;
    size_t envc = 0;
    const char *const *envp = NULL;
    if (dominit0_process_environment_get_sysv(environment, &envc, &envp) != 0) {
        free(state);
        return NULL;
    }
    if (load_and_start_domain(&state->process, elf_data, elf_size,
                              seL4_MaxPrio, argc, argv, envc, envp,
                              program_page_allocator_prepare,
                              program_page_allocator_cleanup, state,
                              seL4_CapNull) != 0) {
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
