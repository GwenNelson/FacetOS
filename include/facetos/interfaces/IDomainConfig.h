#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <facetos/uuid.h>
#include <facetos/interfaces/IGenericObject.h>
#include <facetos/interfaces/ILoggingConfig.h>
#include <facetos/interfaces/IDomainConsoleConfig.h>

static const uuid_t IID_IDomainConfig = UUID_INIT(0xa9de0307, 0xc51f, 0x4d3a, 0x9740, 0xcfdadffaf33bULL);

static const char IDomainConfig_InterfaceName[] = "IDomainConfig";


static const uuid_t IDomainConfig_RequiredInterfaces[] = {
    IID_IGenericObject,
};

static const size_t IDomainConfig_RequiredInterfacesCount =
    sizeof(IDomainConfig_RequiredInterfaces) /
    sizeof(IDomainConfig_RequiredInterfaces[0]);

typedef enum {
    IDomainConfig_DomainPersonality_Native = 0,
    IDomainConfig_DomainPersonality_Posix  = 1,
} IDomainConfig_DomainPersonality;

typedef enum {
    IDomainConfig_DomainManagerMode_None   = 0,
    IDomainConfig_DomainManagerMode_Local  = 1,
    IDomainConfig_DomainManagerMode_Parent = 2,
} IDomainConfig_DomainManagerMode;

typedef struct IDomainConfig {
    void *self; // required by ALL interfaces
    void *priv; // private data

    void *(*getInterface)(void *self, uuid_t iid);

    uint64_t    domain_id;
    const char *domain_name;

    IDomainConfig_DomainPersonality personality;

    ILoggingConfig       logger_config;
    IDomainConsoleConfig console_config;

    bool spawn_console_server;

    IDomainConfig_DomainManagerMode domain_manager;

} IDomainConfig;
