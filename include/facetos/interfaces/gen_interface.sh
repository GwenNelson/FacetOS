#!/bin/bash

if [ $# -eq 0 ]; then
    echo "FacetOS interface template generator"
    echo "Usage: $0 <interface_name>"
    exit 1
fi

interface_name="$1"
uuid=$(uuidgen)
uuid_no_dash=${uuid//-/}

cat << EOF
#pragma once

#include <facetos/uuid.h>
#include <facetos/interfaces/IGenericObject.h>

#include <stddef.h>

static const uuid_t IID_${interface_name} = UUID_INIT(0x${uuid_no_dash:0:8},0x${uuid_no_dash:8:4},0x${uuid_no_dash:12:4},0x${uuid_no_dash:16:4},0x${uuid_no_dash:20:12}ULL);

static const char ${interface_name}_InterfaceName[] = "${interface_name}";

static const size_t uuid_t ${interface_name}_RequiredInterfaces[] = {
	IID_IGenericObject, // this is needed by everything
};

static const  ${interface_name}_RequiredInterfacesCount = sizeof(${interface_name}_RequiredInterfaces) / sizeof(${interface_name}_RequiredInterfaces[0]);

typedef struct ${interface_name} {
	void   *self; // required by ALL interfaces
	void   *priv; // private data
	void* (*getInterface)(void* self, uuid_t iid);  // Returns the requested interface exposed by this object,
							// or NULL if the object does not support it.
	// fill in your methods and variables here, remember to always make first argument void* self
} ${interface_name};
EOF

