#pragma once

#include <stddef.h>
#include <stdint.h>

#include <facetos/uuid.h>
#include <facetos/interfaces/IGenericObject.h>

static const uuid_t IID_ILoggingConfig =
    UUID_INIT(0xde78131b, 0xb3d3, 0x4783, 0x98c5, 0xa4c44daafb59ULL);

static const uuid_t ILoggingConfig_RequiredInterfaces[] = {
    IID_IGenericObject,
};

static const size_t ILoggingConfig_RequiredInterfacesCount =
    sizeof(ILoggingConfig_RequiredInterfaces) /
    sizeof(ILoggingConfig_RequiredInterfaces[0]);

typedef int32_t ILoggingConfig_LogLevel;

enum {
    ILoggingConfig_LogLevel_None    = 0,
    ILoggingConfig_LogLevel_Fatal   = 10,
    ILoggingConfig_LogLevel_Error   = 20,
    ILoggingConfig_LogLevel_Warning = 30,
    ILoggingConfig_LogLevel_Info    = 40,
    ILoggingConfig_LogLevel_Debug   = 50,
    ILoggingConfig_LogLevel_Trace   = 60,
};

typedef struct ILoggingConfig_Sink {
    const char *name;
    ILoggingConfig_LogLevel level;
} ILoggingConfig_Sink;

typedef struct ILoggingConfig {
    void *self; // required by ALL interfaces
    void *priv; // private data

    void *(*getInterface)(void *self, uuid_t iid);

    size_t               sink_count;
    ILoggingConfig_Sink *sinks;

} ILoggingConfig;
