# FacetOS

FacetOS is a capability-oriented seL4 system. `dominit0` parses `config/facet.toml`, starts seat servers, and creates configured domains. Each domain receives a typed `IDomainEnvironment`; platform-specific seL4 state stays behind dominit0 and the platform launcher.

The default configuration has native domain 0 on serial `seat0.ttyS0` and local `seat1.tty1`. Local `seat1.tty2` is a POSIX view of domain 0: it runs `/bin/login` with only `IPOSIXView`, never native process/auth/filesystem capabilities. Its `/etc` is virtual and synthesized by dominit0; it is not present in `system.initrd`. `seat1.tty3` is domain 1, a pure POSIX domain whose `/sbin/init` is PID 1 and whose `child.initrd` contains its own `/etc`.

## Architecture

![FacetOS architecture](docs/architecture.png)


FacetOS is a capability-oriented operating system built on the seL4 microkernel. seL4 provides the fundamental mechanisms for isolation, memory management, scheduling, IPC and capability-based authority; the higher-level operating-system architecture and APIs are implemented in userspace.

At boot, `dominit0` acts as the privileged root of the FacetOS userspace. It constructs the system environment, starts machine-level services such as seat servers and other drivers, discovers the resources they expose, and delegates those resources to configured domains. It then creates a separate `dominit` instance for each configured domain.

The **system domain** is distinct from the numbered application domains. It contains `dominit0` and machine-level services that must exist outside, or provide resources to, individual domains. Domain 0 is therefore an ordinary configured domain rather than another name for the system domain.

Each configured domain is isolated and managed by its own `dominit`. A domain receives only the capabilities explicitly delegated to it and may use those capabilities to construct its own services, namespaces and process environments. `dominit` is responsible for launching and managing the user processes belonging to that domain.

Domains do not have an intrinsic personality based on their domain number. A domain may host native FacetOS applications, expose a POSIX view, operate as a pure POSIX environment, or eventually host other execution environments such as virtual machines. These are configuration and capability choices rather than properties hard-coded into the kernel or domain ID.

### Native FacetOS API and IDL interfaces

The primary API presented to **native FacetOS applications is its typed object-interface system**.

Operating-system facilities are exposed as objects implementing interfaces defined using the FacetOS Interface Definition Language (IDL). Rather than applications primarily interacting with the OS through a large global table of integer system calls, native applications obtain handles to objects implementing interfaces such as terminal, filesystem, process, authentication or other services and invoke the methods defined by those interfaces.

Conceptually:

    Native application
          │
          │ typed FacetOS API
          ▼
       IWhatever
          │
          │ generated IDL bindings / IPC
          ▼
    service implementing IWhatever
          │
          ▼
    delegated capabilities and lower-level services

IDL definitions specify the contract between clients and services, including methods, properties, argument types and interface relationships. Bindings generated from those definitions provide the native API used by applications while hiding the underlying IPC transport and seL4-specific details.

Object handles also represent authority. A process can use a service only when it has been explicitly given an appropriate handle/capability to an object providing that interface. There is therefore no assumption that every process can access every operating-system service simply because that service exists.

The process environment is the starting point for this object graph. When `dominit` launches a native application, it constructs an `IProcessEnvironment` containing the interfaces and capabilities that process is permitted to use. Applications can discover and follow explicitly provided interfaces from there without requiring direct knowledge of seL4 capabilities, endpoints or the internal location of the service implementation.

This makes the IDL/interface layer both the **native programming API and an architectural boundary**. A service may move into another process, be replaced by another implementation, or be proxied across a domain boundary without requiring applications to depend on its internal implementation.

POSIX applications are deliberately different. They interact with a conventional libc/POSIX API; `libc-posix` translates that API onto the appropriate FacetOS interfaces internally. Ordinary POSIX application source therefore does not need to know about FacetOS IDLs or interfaces at all.

At a high level, the system is arranged as:

- **seL4** — microkernel, isolation, IPC and fundamental capability mechanisms.
- **System domain** — `dominit0`, seat servers, hardware/platform services and drivers not delegated elsewhere.
- **Configured domains** — independently managed environments, each rooted at its own `dominit`.
- **FacetOS IDL/interface layer** — the core typed object API through which native applications consume operating-system services.
- **Native user processes** — applications using FacetOS interfaces supplied through their process environment.
- **POSIX user processes** — applications using conventional POSIX APIs, with `libc-posix` privately translating those operations onto FacetOS services.

This separation keeps mechanism, domain policy and application authority distinct: seL4 supplies the underlying protection mechanisms, `dominit0` controls and delegates machine-level resources, each `dominit` controls its own domain, and applications see only the typed interfaces explicitly provided to them.

## Build and run

Run `make build` to build the kernel, services, applications, and initrds. Use a bounded boot such as `make run TIMEOUT=20`; always supply `TIMEOUT` so QEMU cannot remain running. `make test` runs host checks.

Native applications live in `src/apps/native`; POSIX applications live in `src/apps/posix`. POSIX programs use ordinary SysV `main`, while their CRT obtains the per-process `IPOSIXView` from FacetOS auxv. Add a POSIX target to `src/posix/CMakeLists.txt` and package it into the required initrd rule in `Makefile`.

## Configuration

`config/facet.toml` defines users, seats, domains, initrds, logging, and terminal assignments. Native terminal entries select an initial process and view. A POSIX domain names `pid1` and device-backed terminal assignments. The default development password for both configured accounts is `facetos`; its SHA-256 value is used by the prototype login implementation.
