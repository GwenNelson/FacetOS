#pragma once

#include <stddef.h>
#include <stdint.h>

#include <facetos/uuid.h>
#include <facetos/interfaces/IGenericObject.h>

// TODO - implement ISerializeable and add it to this

static const uuid_t IID_IDomainEnvironment =
    UUID_INIT(0xe391d460, 0x87ef, 0x44ff, 0xb853, 0x035e7932f094ULL);

static const char IDomainEnvironment_InterfaceName[] = "IDomainEnvironment";

static const uuid_t IDomainEnvironment_RequiredInterfaces[] = {
    IID_IGenericObject,
};

static const size_t IDomainEnvironment_RequiredInterfacesCount =
    sizeof(IDomainEnvironment_RequiredInterfaces) /
    sizeof(IDomainEnvironment_RequiredInterfaces[0]);

typedef uint64_t IDomainEnvironment_Handle;

typedef struct IDomainEnvironment_Entry {
    const char               *name;
    uuid_t                    iid;
    IDomainEnvironment_Handle handle;
} IDomainEnvironment_Entry;

typedef struct IDomainEnvironment {
    void *self; // required by ALL interfaces
    void *priv; // private data

    void *(*getInterface)(void *self, uuid_t iid);

    size_t                    entry_count;
    IDomainEnvironment_Entry *entries;

    int (*assign)(
        void *self,
        const char *name,
        uuid_t iid,
        IDomainEnvironment_Handle handle
    );

} IDomainEnvironment;
