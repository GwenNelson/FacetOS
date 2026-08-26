#pragma once

#include <facetos/libfacet/common.h>

#include <stdbool.h>
#include <stdint.h>

#include <facetos/initrd.h>

typedef struct Dominit0CredentialFileStore Dominit0CredentialFileStore;

Dominit0CredentialFileStore *dominit0_credential_file_store_create(
    FacetInitrd *initrd, uint32_t uid, uint32_t gid, bool admin);
FacetHandle dominit0_credential_file_store_handle(
    const Dominit0CredentialFileStore *store);
void dominit0_credential_file_store_destroy(
    Dominit0CredentialFileStore *store);
