# FacetOS

FacetOS is a capability-oriented operating system built on the seL4
microkernel.

The short version:

-   **seL4** provides isolation, scheduling, IPC, memory-management
    primitives, and capabilities.
-   **`dominit0`** starts the FacetOS userspace, launches machine-level
    services, and creates configured domains.
-   **Each configured domain has its own `dominit`**, which manages that
    domain and launches its processes.
-   **Native FacetOS programs use typed object interfaces defined in
    IDL.**
-   **POSIX programs use ordinary POSIX APIs** through `libc-posix`;
    they do not need to know about FacetOS interfaces.

## Quick start

``` sh
make build
make test
make run TIMEOUT=20
```

Always give `make run` a `TIMEOUT` so QEMU cannot remain running
indefinitely.

## Architecture

![FacetOS architecture](docs/architecture.png)

FacetOS keeps the microkernel, machine-level services, domains, and
applications as separate layers.

### seL4

seL4 is the foundation. It provides the low-level protection mechanisms
FacetOS builds on: address spaces, scheduling, IPC, memory-management
primitives, and capability-based authority.

Higher-level operating-system policy lives in userspace.

### System domain

The **system domain** contains `dominit0` and machine-level services
such as seat servers and drivers that are not owned by another domain.

At boot, `dominit0`:

1.  reads `config/facet.toml`;
2.  starts machine-level services such as the seat servers;
3.  discovers resources exposed by those services;
4.  assigns resources to configured domains; and
5.  starts one `dominit` for each configured domain.

The system domain is **not domain 0**. Domain 0 is an ordinary
configured domain, just like domain 1 or any future domain.

Platform-specific seL4 state remains behind `dominit0` and the platform
launcher rather than being exposed directly to applications.

### Configured domains

Each configured domain is isolated and has its own `dominit`.

`dominit` receives the capabilities delegated to its domain and uses
them to construct that domain's services, namespaces, process
environments, and user processes.

A domain number does **not** determine what kind of domain it is.
Depending on configuration, a domain may be:

-   a native FacetOS domain;
-   a native domain that also provides a POSIX view;
-   a pure POSIX domain;
-   or, eventually, another kind of execution environment such as a
    virtual machine.

Domain 0 and domain 1 are simply the two configurations currently used
by the development system.

## Native FacetOS API

The core API for native FacetOS software is the **typed object-interface
system**.

Operating-system services are exposed as objects implementing interfaces
defined with the FacetOS Interface Definition Language (IDL). Native
applications receive handles to those objects and call their typed
methods.

For example:

``` text
Native application
        │
        │ typed FacetOS API
        ▼
    IWhatever
        │
        │ generated IDL bindings / IPC
        ▼
Service implementing IWhatever
        │
        ▼
Delegated capabilities and lower-level services
```

IDL definitions describe the contract between a client and a service:
methods, properties, argument types, and interface relationships.
Generated bindings hide the underlying IPC transport and seL4-specific
details from normal application code.

This interface layer is therefore both:

-   the **native FacetOS programming API**; and
-   an **architectural boundary** between applications and service
    implementations.

A service can move to another process, be replaced, or be proxied across
a domain boundary without requiring applications to depend on its
implementation details.

### Handles are authority

FacetOS does not assume that every process can access every
operating-system service.

A process can use an object only when it has been given an appropriate
handle/capability to that object.

When `dominit` launches a native process, it constructs an
`IProcessEnvironment` containing the interfaces and capabilities that
process is allowed to use. Native applications begin with that
environment and discover the services available to them from there.

Applications therefore do not need direct knowledge of seL4 endpoints,
kernel capabilities, or where a service happens to run.

## POSIX support

POSIX applications deliberately see a different API.

Ordinary POSIX source uses conventional libc interfaces. `libc-posix`
translates those operations onto FacetOS services internally, using the
process's `IPOSIXView`.

In other words:

``` text
POSIX application
        │
        │ POSIX / libc API
        ▼
    libc-posix
        │
        │ FacetOS interfaces
        ▼
    IPOSIXView
        │
        ▼
Domain services
```

The application itself does not need to include FacetOS IDL headers or
call FacetOS-specific interfaces.

POSIX programs use an ordinary SysV `main`. Their CRT obtains the
process's `IPOSIXView` from the FacetOS auxiliary vector before entering
the application.

## Current development configuration

The current `facet.toml` demonstrates several kinds of process
environment:

  Terminal        Domain     Environment
  --------------- ---------- ------------------------
  `seat0.ttyS0`   domain 0   native FacetOS
  `seat1.tty1`    domain 0   native FacetOS
  `seat1.tty2`    domain 0   POSIX view of domain 0
  `seat1.tty3`    domain 1   pure POSIX domain

On `seat1.tty2`, `/bin/login` receives only its POSIX view rather than
native process, authentication, or filesystem capabilities. Its `/etc`
is virtual and is not physically present in `system.initrd`.

Domain 1 is currently configured as a pure POSIX domain. Its
`/sbin/init` runs as PID 1, and `child.initrd` contains that domain's
own `/etc`.

These are properties of the **current configuration**, not special
meanings attached to domain numbers.

## Source layout

Native applications live in:

``` text
src/apps/native
```

POSIX applications live in:

``` text
src/apps/posix
```

To add a POSIX application, add its target to `src/posix/CMakeLists.txt`
and package it into the appropriate initrd from the `Makefile`.

## Configuration

`config/facet.toml` defines the development system's:

-   users;
-   seats and terminals;
-   domains;
-   initrds;
-   logging; and
-   terminal assignments.

Native terminal entries select an initial process and view. A POSIX
domain specifies its `pid1` and device-backed terminal assignments.

The prototype login currently uses SHA-256 password hashes. The default
development password for both configured accounts is `facetos`.
