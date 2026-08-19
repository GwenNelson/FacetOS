# FacetOS C Interface Header Specification

## 1. Purpose

FacetOS public interfaces use a consistent C representation so that
interfaces remain easy to recognize, implement, exchange, document, and
eventually expose across IPC boundaries.

This document defines the conventions for public `IWhatever` interface
headers.

A basic interface header should normally be created using:

``` sh
./gen_interface.sh IWhatever
```

`gen_interface.sh` constructs the standard interface template, including
the normal header boilerplate, interface UUID declaration, and basic
interface structure. The generated file should then be extended with the
interface-specific types, fields, and methods described below.

------------------------------------------------------------------------

## 2. Header location and naming

Public FacetOS interfaces normally live under:

``` text
include/facetos/
```

An interface named:

``` c
IFoo
```

normally lives in:

``` text
include/facetos/IFoo.h
```

Interface headers should use:

``` c
#pragma once
```

and include only the headers required by their public definitions.

------------------------------------------------------------------------

## 3. Interface names

Public interfaces use the `IWhatever` naming convention.

For example:

``` c
typedef struct ILoggingConfig {
    ...
} ILoggingConfig;
```

The interface UUID is named:

``` c
IID_ILoggingConfig
```

and declared using `uuid_t` and `UUID_INIT`:

``` c
#include <facetos/uuid.h>

static const uuid_t IID_ILoggingConfig =
    UUID_INIT(0x..., 0x..., 0x..., 0x..., 0x...ULL);
```

The UUID is part of the identity of the interface and must not be
changed merely because the implementation changes.

------------------------------------------------------------------------

## 4. Standard interface prefix

Every FacetOS interface structure begins with these two fields, in this
order:

``` c
void *self;
void *priv;
```

For example:

``` c
typedef struct IFoo {
    void *self; /* required by ALL interfaces */
    void *priv; /* private implementation data */

    ...
} IFoo;
```

### `self`

`self` identifies the object implementing the interface.

Interface methods normally receive it as their first argument:

``` c
int (*doSomething)(void *self, int value);
```

and are called as:

``` c
foo->doSomething(foo->self, value);
```

### `priv`

`priv` is reserved for private implementation-specific state.

Consumers of an interface must not depend on the representation or
contents of `priv`.

The existence of `priv` does **not** mean that public data fields must
be redundantly hidden behind accessors. Public interface state may be
stored directly in the interface structure where appropriate.

------------------------------------------------------------------------

## 5. Interfaces may contain public data

FacetOS interfaces are not required to consist entirely of function
pointers.

If a value is naturally part of the public representation of an
interface, it should normally be represented directly.

Prefer:

``` c
config->logLevel = ILoggingConfig_LogLevel_Debug;
```

over unnecessary accessor machinery such as:

``` c
config->setLogLevel(config->self, ILoggingConfig_LogLevel_Debug);
```

when the implementation would merely store the same value in `priv`.

This is particularly appropriate for data/configuration interfaces such
as:

``` text
IDomainConfig
ILoggingConfig
ISystemConfig
```

Methods should be used when actual behaviour, computation, validation,
indirection, or abstraction is required.

------------------------------------------------------------------------

## 6. Interface-owned type namespace

Any enum, typedef, structure, flag type, or other public type that
conceptually belongs to a particular interface must be namespaced using
that interface's name.

The general form is:

``` text
I<Interface>_<Type>
```

For named values belonging to that type:

``` text
I<Interface>_<Type>_<Value>
```

For example:

``` c
ILoggingConfig_LogLevel
ILoggingConfig_LogLevel_Debug
IDomainConfig_Personality
IDomainConfig_Personality_POSIX
```

Do not introduce generic public names such as:

``` c
LogLevel
DomainPersonality
LOG_LEVEL_DEBUG
DOMAIN_TYPE_POSIX
```

when those concepts belong to a particular interface.

This convention provides a predictable pseudo-namespace despite C having
a single global identifier namespace.

------------------------------------------------------------------------

## 7. Interface-owned structures and typedefs

The same naming rule applies to structures and other typedefs.

For example:

``` c
typedef struct IDomainConfig_Console {
    ...
} IDomainConfig_Console;
```

or:

``` c
typedef uint32_t IDomainConfig_Flags;
```

If a type genuinely belongs to several unrelated interfaces rather than
one particular interface, it should be promoted into an appropriate
common FacetOS type or separate interface instead of being arbitrarily
assigned to one interface's namespace.

------------------------------------------------------------------------

## 8. ABI-visible enumerated values

ABI-visible enumerated values should have explicit, stable integer
representations.

Where the integer representation matters, prefer a fixed-width typedef
plus named constants rather than relying on the compiler-selected
storage representation of a C `enum`.

For example:

``` c
#include <stdint.h>

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
```

This guarantees that:

``` c
ILoggingConfig_LogLevel
```

has a known ABI-visible storage type, while each named value has an
explicitly defined numeric representation.

### Numeric values are ABI

Once published, numeric values form part of the interface ABI.

They must not be implicitly changed by reordering declarations or
inserting new values.

Explicit numbering is therefore required for ABI-visible enumerated
values.

### Leave useful numeric gaps

Where values form an ordered scale, leave gaps when practical:

``` text
0, 10, 20, 30, 40, 50, 60
```

rather than:

``` text
0, 1, 2, 3, 4, 5, 6
```

This allows future values to be inserted without renumbering existing
ABI values.

For example, a future logging level could be added at `35` without
changing the established values of `Info`, `Debug`, or `Trace`.

------------------------------------------------------------------------

## 9. Ordered configuration values

Where a configuration value is intentionally ordered, its numeric values
should be selected so normal comparisons express the desired policy.

For logging, FacetOS currently uses increasing values for increasing
verbosity:

``` c
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
```

This permits a logger to use the simple test:

``` c
if (curLevel >= level) {
    /* emit message */
}
```

For example, an `Info` configuration (`40`) includes Fatal, Error,
Warning, and Info messages while excluding Debug and Trace.

The ordering and numeric meaning should be documented when comparison
semantics are part of the interface.

------------------------------------------------------------------------

## 10. Constants and flags

Interface-specific constants must use the interface namespace.

For example:

``` c
#define IDomainConfig_MaxNameLength 64
```

Flags should similarly use an interface-owned type and names:

``` c
typedef uint32_t IDomainConfig_Flags;

enum {
    IDomainConfig_Flag_None      = 0,
    IDomainConfig_Flag_Something = 1u << 0,
};
```

Avoid generic names such as:

``` c
MAX_NAME
FLAG_DEBUG
TYPE_NATIVE
```

in public headers.

------------------------------------------------------------------------

## 11. Methods

Behaviour exposed by an interface is represented using function pointers
in the interface structure.

For example:

``` c
typedef struct IFoo {
    void *self;
    void *priv;

    int (*doSomething)(void *self, int value);
} IFoo;
```

The normal calling convention is:

``` c
foo->doSomething(foo->self, value);
```

The first method parameter should therefore normally be:

``` c
void *self
```

unless an interface has a specific ABI reason to use another convention.

Methods should be added for behaviour rather than merely to disguise
straightforward public data fields.

------------------------------------------------------------------------

## 12. Embedded interfaces

An interface may contain another interface when the contained concept
has an independently useful FacetOS abstraction.

For example:

``` c
typedef struct IDomainConfig {
    void *self;
    void *priv;

    uint64_t domainID;

    ILoggingConfig logging;

    ...
} IDomainConfig;
```

Embedding an interface by value is appropriate when:

-   the contained interface is an intrinsic part of the containing
    object;
-   it is always present; and
-   its lifetime naturally follows the containing object.

This permits straightforward use such as:

``` c
config->logging.level = ILoggingConfig_LogLevel_Debug;
```

A pointer should normally be used instead when the referenced interface:

-   represents an independently owned object or service;
-   may be absent;
-   has an independent lifetime; or
-   is supplied/delegated by another object.

Not every conceptual grouping needs to become an interface. Small
implementation-local or purely structural groupings may use ordinary
interface-owned structs where that is simpler.

------------------------------------------------------------------------

## 13. Configuration interfaces

Configuration is a legitimate use of FacetOS interfaces.

For example, `ILoggingConfig` may initially be:

``` c
#pragma once

#include <stdint.h>

#include <facetos/uuid.h>

static const uuid_t IID_ILoggingConfig =
    UUID_INIT(0x..., 0x..., 0x..., 0x..., 0x...ULL);

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

typedef struct ILoggingConfig {
    void *self; /* required by ALL interfaces */
    void *priv; /* private implementation data */

    ILoggingConfig_LogLevel level;
} ILoggingConfig;
```

Other systems, such as `klog`, may later consume `ILoggingConfig`
directly rather than maintaining a separate incompatible logging
configuration representation.

------------------------------------------------------------------------

## 14. Platform independence

Generic FacetOS public interfaces should remain independent of the
underlying microkernel or platform wherever practical.

A generic interface such as `IDomainConfig` should not expose
implementation-specific types such as:

``` c
seL4_CPtr
seL4_BootInfo *
vka_t *
vspace_t *
```

unless the interface is explicitly defined as seL4-specific.

Platform adapters are responsible for translating platform-specific
resources and mechanisms into generic FacetOS concepts.

This keeps interfaces usable by code that does not know or care whether
the underlying implementation uses seL4, another microkernel, a hosted
environment, or some future platform.

------------------------------------------------------------------------

## 15. Configuration is not authority

Configuration interfaces describe intended policy or behaviour.

They must not be confused with the capabilities, resources, or delegated
interfaces that grant actual authority.

For example, an `IDomainConfig` value may state that a domain should use
domain management supplied by its parent. That configuration does not
itself grant domain-management authority.

Actual authority must come from the environment and
capabilities/interfaces made available to the object or domain.

This distinction should be preserved in interface design:

``` text
configuration
    = what should this object/domain do?

environment / delegated interfaces
    = what is it actually able and permitted to do?
```

------------------------------------------------------------------------

## 16. String and pointer fields

Pointers in public interfaces require an explicit lifetime/ownership
contract.

A field such as:

``` c
const char *domainName;
```

must eventually define whether the pointed-to string is:

-   borrowed;
-   owned by the interface;
-   immutable for the interface lifetime;
-   replaceable;
-   dynamically allocated; or
-   backed by a larger configuration/environment buffer.

Until a general FacetOS ownership convention is established, interface
documentation should state the lifetime expectations of public pointer
fields where ambiguity could matter.

Raw pointers are not inherently suitable for serialization or
cross-domain IPC. An IPC representation may therefore differ from the
in-process C interface representation.

------------------------------------------------------------------------

## 17. In-process representation versus IPC representation

The public C interface structure is the convenient in-process
representation of an interface.

It should not be assumed that the exact memory layout of that structure
will be copied verbatim across IPC boundaries.

Pointers such as:

``` c
void *self;
void *priv;
const char *name;
```

are process-local values.

Generated IPC proxies/stubs or other serialization machinery may
represent the same interface semantics differently.

Stable interface UUIDs, stable ABI-visible value definitions, and
clearly specified method/data semantics allow the transport
representation to evolve independently.

------------------------------------------------------------------------

## 18. Header dependencies

Interface headers should include the definitions required to use the
interface without relying on accidental include order.

For example, if an interface publicly contains:

``` c
uint64_t domainID;
```

it should include:

``` c
#include <stdint.h>
```

If it embeds:

``` c
ILoggingConfig logging;
```

it should include the public header that defines `ILoggingConfig`.

Avoid unnecessary dependencies on implementation headers.

------------------------------------------------------------------------

## 19. Generated interface template

`gen_interface.sh` is the normal starting point for a new interface.

A generated basic interface should resemble:

``` c
#pragma once

#include <facetos/uuid.h>

static const uuid_t IID_IFoo =
    UUID_INIT(0x..., 0x..., 0x..., 0x..., 0x...ULL);

typedef struct IFoo {
    void *self; /* required by ALL interfaces */
    void *priv; /* private implementation data */

} IFoo;
```

The developer then adds interface-owned types before the main interface
structure and adds public fields/methods to the structure as required.

For example:

``` c
#pragma once

#include <stdint.h>

#include <facetos/uuid.h>

static const uuid_t IID_IFoo =
    UUID_INIT(0x..., 0x..., 0x..., 0x..., 0x...ULL);

typedef int32_t IFoo_Mode;

enum {
    IFoo_Mode_None = 0,
    IFoo_Mode_A    = 10,
    IFoo_Mode_B    = 20,
};

typedef struct IFoo {
    void *self; /* required by ALL interfaces */
    void *priv; /* private implementation data */

    IFoo_Mode mode;

    int (*doSomething)(void *self, int value);
} IFoo;
```

------------------------------------------------------------------------

## 20. Summary of naming conventions

  -------------------------------------------------------------------------------------
  Concept                 Convention               Example
  ----------------------- ------------------------ ------------------------------------
  Interface               `IWhatever`              `ILoggingConfig`

  Header                  `IWhatever.h`            `ILoggingConfig.h`

  Interface UUID          `IID_IWhatever`          `IID_ILoggingConfig`

  Interface-owned type    `IWhatever_Type`         `ILoggingConfig_LogLevel`

  Interface-owned value   `IWhatever_Type_Value`   `ILoggingConfig_LogLevel_Debug`

  Interface-owned struct  `IWhatever_Name`         `IDomainConfig_Console`

  Interface-owned         `IWhatever_Name`         `IDomainConfig_MaxNameLength`
  constant                                         

  Interface-owned flag    `IWhatever_Flag_Name`    `IDomainConfig_Flag_Debug`

  Method                  function pointer in      `foo->doSomething(foo->self, ...)`
                          interface                
  -------------------------------------------------------------------------------------

------------------------------------------------------------------------

## 21. Core design rules

When designing a new FacetOS C interface:

1.  Start with `gen_interface.sh`.
2.  Give every interface a stable UUID.
3.  Keep `self` and `priv` as the first two interface fields.
4.  Namespace interface-owned types and values as `IWhatever_Something`.
5.  Give ABI-visible enumerated values explicit stable numbers.
6.  Prefer fixed-width typedefs when the integer representation itself
    is ABI-visible.
7.  Leave numeric gaps where future ordered values may need insertion.
8.  Put straightforward public data directly in data/configuration
    interfaces rather than creating redundant accessors.
9.  Use methods for actual behaviour or abstraction.
10. Embed interfaces by value when they are intrinsic and share
    lifetime; use pointers for independent, optional, or delegated
    interfaces.
11. Keep generic interfaces platform-independent.
12. Keep configuration separate from actual authority/resources.
13. Specify ownership/lifetime for public pointer fields.
14. Do not assume the in-process C structure is itself the IPC wire
    format.
15. Include the public headers required by the interface's own
    declarations.

These conventions are intended to keep FacetOS interfaces predictable,
inspectable, and evolvable without turning ordinary C data access into
unnecessary ceremony.
