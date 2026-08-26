#pragma once

#include <facetos/interfaces/IGenericObject.h>

typedef struct IProcessEnvironment IProcessEnvironment;

IGenericObject *facet_native_root(void);
IProcessEnvironment *facet_native_environment(void);

