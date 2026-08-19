#pragma once

#include <stddef.h>

#include <facetos/uuid.h>
#include <facetos/interfaces/IGenericObject.h>

static const uuid_t IID_IDomainConsoleConfig =
    UUID_INIT(0x5a772cdf, 0x72f1, 0x43c7, 0x89ae, 0xb9a71a8e8242ULL);

static const uuid_t IDomainConsoleConfig_RequiredInterfaces[] = {
    IID_IGenericObject,
};

static const size_t IDomainConsoleConfig_RequiredInterfacesCount =
    sizeof(IDomainConsoleConfig_RequiredInterfaces) /
    sizeof(IDomainConsoleConfig_RequiredInterfaces[0]);

typedef struct IDomainConsoleConfig_Assignment {
    const char *seat;     // seat to attach to, e.g. "seat0"
    const char *terminal; // terminal on that seat, e.g. "tty1", "ttys0"
} IDomainConsoleConfig_Assignment;

typedef struct IDomainConsoleConfig {
    void *self; // required by ALL interfaces
    void *priv; // private data

    void *(*getInterface)(void *self, uuid_t iid);

    size_t assignment_count;
    IDomainConsoleConfig_Assignment *assignments;

} IDomainConsoleConfig;
