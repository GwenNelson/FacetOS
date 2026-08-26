#pragma once

#include <stddef.h>
#include <stdint.h>

/* ELF reserves this interval for operating-system auxiliary-vector keys. */
#ifndef AT_LOOS
#define AT_LOOS 0x60000000UL
#endif
#ifndef AT_HIOS
#define AT_HIOS 0x6fffffffUL
#endif

#define FACETOS_STARTUP_ABI_VERSION 1UL

/* Common process bootstrap range. These numbers are ABI and must not move. */
#define AT_FACET_ABI_VERSION    (AT_LOOS + 0x000UL)
#define AT_FACET_ROOT_OBJECT    (AT_LOOS + 0x001UL)
#define AT_FACET_RECEIVE_CNODE  (AT_LOOS + 0x002UL)
#define AT_FACET_RECEIVE_SLOT   (AT_LOOS + 0x003UL)
#define AT_FACET_RECEIVE_DEPTH  (AT_LOOS + 0x004UL)
/* Optional local service endpoint.  Domain bootstrap processes use this to
 * export their domain-owned authority while retaining the common client ABI. */
#define AT_FACET_SERVICE_ENDPOINT     (AT_LOOS + 0x005UL)
#define AT_FACET_SERVICE_RECEIVE_SLOT (AT_LOOS + 0x006UL)
#define AT_FACET_SERVICE_EXPORT_SLOT  (AT_LOOS + 0x007UL)

/* Domain-bootstrap range reserved for future domain-only startup data. */
#define AT_FACET_DOMAIN_BASE    (AT_LOOS + 0x100UL)

/* Isolated seat-server bootstrap range. */
#define AT_FACET_SEAT_SERVICE_ENDPOINT (AT_LOOS + 0x200UL)
#define AT_FACET_SEAT_READY_ENDPOINT   (AT_LOOS + 0x201UL)
#define AT_FACET_SEAT_DEVICE0          (AT_LOOS + 0x202UL)
#define AT_FACET_SEAT_DEVICE1          (AT_LOOS + 0x203UL)
#define AT_FACET_SEAT_RECEIVE_SLOT     (AT_LOOS + 0x204UL)
#define AT_FACET_SEAT_EXPORT_SLOT      (AT_LOOS + 0x205UL)
#define AT_FACET_SEAT_CNODE            (AT_LOOS + 0x206UL)
#define AT_FACET_SEAT_DEPTH            (AT_LOOS + 0x207UL)
#define AT_FACET_SEAT_VGA_ADDRESS      (AT_LOOS + 0x208UL)

#if AT_FACET_SEAT_VGA_ADDRESS > AT_HIOS
#error "FacetOS auxiliary-vector allocation exceeds AT_HIOS"
#endif

typedef struct FacetAuxvEntry {
    uintptr_t type;
    uintptr_t value;
} FacetAuxvEntry;

/* Called by the FacetOS CRT before main(). Applications normally use getenv()
 * and getauxval() rather than accessing this startup state directly. */
void facet_libc_initialize(char *const environment[],
                           const FacetAuxvEntry auxiliary_vector[]);
char *const *facet_libc_environment(void);
const FacetAuxvEntry *facet_libc_auxiliary_vector(void);
int facet_auxv_validate(size_t count, const FacetAuxvEntry entries[]);

unsigned long getauxval(unsigned long type);
