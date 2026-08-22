# FacetOS System Configuration and Service Architecture

## 1. Status and purpose

This document specifies the intended architecture for boot configuration,
logging, domain and process environments, process personalities, terminals,
device discovery, and the initial system bootstrap sequence.

It is intended to be detailed enough to guide implementation. Terms such as
MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are normative.

This document builds on the Facet object and RPC model described by the
top-level `spec.md` and by `idl/README.md`. It does not replace the libfacet
wire ABI. In particular:

- interface instances are real C objects;
- all interfaces contain the methods declared by `IGenericObject`;
- remotely returned objects are represented by transferred `FacetHandle`s and
  newly constructed client proxies;
- freeing a proxy and releasing its handle are distinct operations;
- portable code never inspects the platform representation of a
  `FacetHandle`;
- `libfacet-common` remains independent of seL4;
- generated interfaces and portable services remain transport-independent.

`DESIGN_NOTES.md` remains useful background material, but it contains older,
exploratory decisions. Where this document makes a definite decision about a
subsystem covered below, this document is authoritative.

The first implementation target is seL4. Nothing above the platform adapter
may assume seL4 semantics unless this document explicitly says otherwise.

## 2. Fundamental security model

The central rule is:

> Configuration describes intended behaviour. Capabilities determine actual
> authority.

A name, domain number, executable path, configuration declaration, registry
entry, or claim to be a manager never grants authority. Authority exists only
when the process holds an appropriate Facet object handle or an underlying
platform capability, or when an authorized service agrees to perform an
operation on its behalf.

Consequences include:

- A process calling itself `dominit0` or Domain 0 gains no special rights.
- A service entry in configuration is only a request to bind a name to an
  object. The binding is usable only if the parent can and does delegate the
  corresponding handle.
- Driver matching metadata does not grant access to a device.
- A numeric domain ID is an identifier within one hierarchy, not an authority
  token.
- A process environment is a filtered authority view, not a copy of a global
  registry.
- Child domains never receive the complete root configuration merely because
  they were described by it.
- Revoking or destroying a delegated handle removes authority regardless of
  what stale configuration still says.

Configuration and incoming RPC are untrusted input. All lengths, counts,
indices, names, UUIDs, method arguments, transferred handles, and allocation
sizes must be validated before use.

## 3. Architectural layers

The initial system is divided into the following logical layers.

### 3.1 Platform bootstrap

The platform bootstrap is the smallest layer permitted to understand the
native kernel ABI. On seL4 it consumes `seL4_BootInfo`, initializes allocman,
VKA and VSpace support, identifies multiboot modules, and provides the
platform implementation of libfacet.

It must translate raw platform resources into Facet objects before handing
them to portable domain code. It must not require `dominit` to manipulate
seL4 capabilities, CSpace paths, VSpaces, endpoints, or message registers.

### 3.2 dominit0

`dominit0` is the initial resource owner and policy-enforcement process for a
native FacetOS domain. For the root domain it receives the authority supplied
by the kernel. For a child domain it receives only authority delegated by its
parent.

Its responsibilities include:

- bringing up enough memory management to parse configuration safely;
- constructing the domain's logger and platform sinks;
- turning configuration requests into concrete, filtered object bindings;
- creating child address spaces and process-scoped allocators;
- exporting root environment objects;
- loading the configured domain bootstrap process;
- managing devices and child domains when it has authority to do so.

`dominit0` is not itself what makes something a domain. A POSIX personality or
a VMM may be the principal workload of a domain constructed by a parent.

### 3.3 Domain bootstrap or personality process

Every domain has one initial process that receives an `IDomainEnvironment` as
its root object:

- a native domain normally starts `dominit`;
- a pure POSIX domain starts its POSIX personality server and then PID 1;
- a VM domain starts a VMM;
- a future personality may use another bootstrap implementation.

The bootstrap process receives only the authority selected for that domain.
It may construct additional processes and environments only through services
that were delegated to it.

### 3.4 Ordinary processes

Every ordinary process receives an `IProcessEnvironment` as its startup root
object. It does not receive `IDomainEnvironment` unless it is specifically the
domain bootstrap process.

The process environment contains only objects intended for that process. It
is immutable from the child's point of view.

### 3.5 Personality services

A personality translates a selected set of Facet services into the execution
model expected by a workload. The first personalities are:

- native FacetOS;
- POSIX compatibility inside a native domain;
- a pure POSIX domain;
- a virtual machine.

The parent-facing domain and capability model is the same regardless of
personality.

## 4. Root configuration

### 4.1 Source and selection

The root configuration MUST be a distinct multiboot module. The module's
basename is `facet.toml`. Module command-line text following the first token
does not form part of the basename.

There MUST be exactly one `facet.toml` module in a normal configured boot.
Missing or duplicate configuration modules are fatal after the emergency
logger is available. A temporary compile-time development option MAY permit
built-in defaults when the module is absent, but that option must be disabled
for a normal configured system.

The configuration module is immutable boot input. It must be preserved until
parsing and validation have completed. No code may retain pointers into it
after its backing module may be reclaimed unless ownership was explicitly
transferred.

### 4.2 Syntax

Version 1 uses a deliberately limited TOML subset:

- UTF-8 text without a byte-order mark;
- bare and quoted keys;
- strings, booleans, and signed 64-bit integers;
- arrays of supported scalar values;
- tables and arrays of tables;
- `#` line comments.

Version 1 does not require floats, date/time literals, dotted assignment keys,
inline tables, or multiline strings. A parser may support them later only as a
schema-version-compatible extension.

The parser MUST reject malformed UTF-8, integer overflow, duplicate keys in a
table, incompatible redefinitions, unterminated strings, excessive nesting,
and data after a complete invalid construct. It must never silently reinterpret
an invalid value.

Initial defensive limits are:

- configuration module size: 1 MiB;
- nesting depth: 16;
- total keys and table entries: 4096;
- individual string: 64 KiB;
- array elements: 4096.

These are implementation resource limits, not permanent ABI constants. A
failure caused by a limit must be distinguishable in diagnostics from a
syntax error.

### 4.3 Versioning and validation

Every file begins with:

```toml
[facet]
version = 1
```

An unsupported major schema version is fatal. Unknown keys in version 1 are
errors rather than silently ignored. Later versions may add an explicit
extension namespace.

Parsing and semantic validation occur before creating configured services or
processes. Validation includes:

- unique domain IDs and names within the hierarchy;
- unique module aliases;
- valid references to modules, domains, seats, terminals, log sinks, and
  service providers;
- valid personality names;
- valid log levels;
- no dependency cycles among required services;
- no duplicate process-environment names;
- valid executable source syntax;
- resource quantities within platform and parent policy limits;
- exactly one root domain description;
- all required declarations resolvable before launch.

Validation proves only that a request is coherent. Authority checks happen
again when concrete handles and resources are delegated.

### 4.4 Precedence

Root-domain effective configuration is constructed in this order:

1. compiled safe defaults;
2. `facet.toml`;
3. explicit boot command-line overrides intended for diagnostics.

Later sources replace earlier scalar values. Arrays are replaced, not
implicitly concatenated, unless a field's schema explicitly says otherwise.
Command-line overrides must use a small documented set; they are not a second
general configuration language.

Children do not receive the root file. A parent selects the relevant domain
description, resolves it into an effective configuration, and creates the
child's environment and delegated capabilities directly.

### 4.5 Modules and executable references

Boot modules receive stable logical aliases in configuration:

```toml
[[modules]]
name = "dominit"
boot_module = "dominit"
format = "elf"
```

`boot_module` is matched against the basename of the multiboot module's first
command-line token. Duplicate basenames are an error when referenced by name.

Executable references use one of these forms:

- `module:<alias>` for a configured boot module;
- an absolute POSIX path such as `/sbin/init`;
- a native executable path under `/FacetOS`, such as
  `/FacetOS/FacetLogin`;
- future schemes explicitly introduced by a later schema version.

An executable name is not authority to read or execute it. The launcher must
hold the module, filesystem, object-store, or VM resource capability needed to
resolve it.

### 4.6 Initial configuration shape

The initial schema has the following top-level tables:

- `[facet]`: schema identity and version;
- `[[modules]]`: aliases for multiboot modules;
- `[logging]` and `[[logging.sinks]]`: root logging policy;
- `[root]`: root-domain description;
- `[[root.services]]`: services visible to the root bootstrap process;
- `[[root.terminals]]`: processes launched on terminals in the root domain;
- `[[domains]]`: child-domain descriptions;
- `[[domains.services]]`: child service requests and bindings;
- `[[domains.terminals]]`: child terminal launch entries;
- `[[seats]]` and `[[seats.terminals]]`: seat topology;
- `[[drivers]]`: configured driver factories or modules.

Example:

```toml
[facet]
version = 1

[[modules]]
name = "dominit"
boot_module = "dominit"
format = "elf"

[logging]
default_level = "info"

[[logging.sinks]]
name = "debug"
type = "platform.sel4.debug"
level = "debug"
required = true

[root]
id = 0
name = "system"
personality = "native"
bootstrap = "module:dominit"
domain_manager = true

[[root.services]]
name = "logger"
interface = "ILogger"
source = "logging.system"
required = true

[[root.services]]
name = "memory.pages"
interface = "IPageAllocator"
source = "memory.bootstrap-process"
required = true

[[root.terminals]]
terminal = "seat1.tty1"
executable = "/FacetOS/FacetLogin"
personality = "native"
required = false

[[root.terminals]]
terminal = "seat1.tty2"
executable = "/bin/login"
personality = "posix"
required = false

[root.terminals.environment]
PATH = "/bin:/usr/bin"
HOME = "/"
TERM = "facet"

[[seats]]
name = "seat0"
type = "serial"

[[seats.terminals]]
name = "ttys0"
active = true

[[seats]]
name = "seat1"
type = "local"

[[seats.terminals]]
name = "tty1"
active = true

[[seats.terminals]]
name = "tty2"
```

The parser should preserve source locations for values so semantic errors can
identify the table, key, line, and column that caused the problem.

## 5. Logging

### 5.1 Separation of policy, emission, and sinks

Logging uses three distinct interfaces:

- `ILoggingConfig` describes immutable effective logging policy;
- `ILogger` is the normal interface applications use to emit records;
- `ILoggerSink` is the privileged destination interface used by a logger.

Applications MUST NOT write through `ILoggingConfig`. A normal domain or
process normally receives an `ILogger`, not its sinks. Possession of a sink
would permit bypassing logger filtering, routing, and attribution policy.

`ILogger` and `ILoggerSink` are transport-independent interfaces. The first
sink implementation is an seL4-platform sink that calls the kernel debug
output operation. Only that implementation and the platform bootstrap include
seL4 headers.

### 5.2 Log levels and records

Version 1 levels are numerically ordered:

```text
None     0
Fatal   10
Error   20
Warning 30
Info    40
Debug   50
Trace   60
```

A record contains at least:

- level;
- component name;
- message bytes represented as a Facet string;
- domain ID when known;
- process ID when known;
- monotonic timestamp in nanoseconds when a clock is available;
- flags indicating which optional attribution fields are valid.

Message text need not end in a newline. Formatting policy belongs to the
sink or logger formatter, not the caller. Version 1 does not require arbitrary
structured fields, but the record format must be extensible.

The initial `ILogger` contract is conceptually:

```text
log(level, component, message) -> FacetResult
flush()                         -> FacetResult
```

The server supplies trustworthy domain/process attribution from the exported
logger object; a client cannot forge it by embedding IDs in its message.

The initial `ILoggerSink` contract is conceptually:

```text
emit(record) -> FacetResult
flush()      -> FacetResult
```

Shared logging types should be placed in one generated type definition and
imported by logging interfaces. Before implementing these IDLs, facet-idlc
must gain a type-only import mechanism; `requires` must not be abused as an
import because it promises runtime `getInterface()` support.

### 5.3 Logger policy

The logger applies all of the following before calling a sink:

- domain/process maximum level;
- optional component-specific maximum level;
- sink maximum level;
- route inclusion/exclusion rules;
- message and queue size limits.

Each process receives either a process-specific logger object or a logger
view whose server-side state fixes its attribution and maximum permitted
verbosity. A child cannot increase its effective verbosity merely by claiming
a lower numeric level in a call.

Sink initialization marked `required = true` is fatal if it fails. Failure of
an optional sink is logged through the emergency sink and the sink is disabled.
A sink failure while emitting must not recursively log through the failing
sink. The logger needs a recursion guard and a minimal emergency-output path.

### 5.4 Early logging and klog migration

Logging needed to debug memory initialization must not allocate memory or use
RPC. Early boot therefore uses a fixed static record buffer and direct
platform debug output.

The transition sequence is:

1. initialize emergency character output;
2. initialize the static early-record buffer;
3. initialize memory and parse/validate configuration;
4. construct configured sinks and the root logger;
5. import early records into the retained log without re-emitting records
   already written to the same emergency sink;
6. atomically switch the common logging facade to the full logger.

`klog` should become the bootstrap-compatible front end to this common
logging facade. Formatting, levels, and record routing must not be separately
reimplemented in `dominit`.

Portable processes should eventually use a small transport-neutral logging
client library layered on `ILogger`. That library is separate from
`libfacet-common`, because `libfacet-common` must remain unaware of
application-specific interfaces.

`dominit` obtains the `logger` binding from its `IProcessEnvironment` as soon
as libfacet bootstrap is complete and uses it for all subsequent diagnostics.

## 6. Domain and process environments

### 6.1 Startup roots

The first platform integer passed to a child identifies one exported Facet
root object. Platform startup code converts it with `libfacet_proxy_from()`.

For a domain bootstrap process, that logical root object MUST provide:

- `IGenericObject`;
- `IDomainEnvironment`;
- `IProcessEnvironment` for the bootstrap process.

Each call to `getInterface()` follows the normal libfacet returned-object
semantics. It produces an appropriate interface instance/proxy and handle; it
is not a C cast.

For an ordinary process, the root object MUST provide:

- `IGenericObject`;
- `IProcessEnvironment`;
- any default personality interface deliberately obtainable from that
  environment, such as `IPOSIXView`.

It MUST NOT provide `IDomainEnvironment` unless the process is the designated
domain bootstrap process.

### 6.2 IDomainEnvironment

`IDomainEnvironment` is a read-only description of one domain's effective
identity and delegated bootstrap context. It contains or exposes:

- domain ID;
- domain name;
- personality (`native`, `posix`, `vm`, or a future registered value);
- a read-only effective `IDomainConfig` view;
- access to the bootstrap process's `IProcessEnvironment` through normal
  `getInterface()` behavior.

Mutable operations do not belong on this interface. Domain creation,
resource allocation, process creation, device management, and seat management
belong on separate privileged interfaces such as `IDomainManager`,
`IProcessManager`, or specific resource-manager interfaces.

The current mutable `entries`/`assign` shape of `IDomainEnvironment` is not the
target design and must be replaced when this subsystem is implemented.

### 6.3 IProcessEnvironment as a typed namespace

`IProcessEnvironment` is a read-only namespace of named logical objects. It is
not a predefined struct of every service a process might ever receive.

Names are case-sensitive UTF-8 strings, limited initially to 127 bytes. The
recommended convention is lower-case dot-separated components, for example:

```text
stdin
stdout
stderr
logger
memory.pages
terminal.control
posix
posix.environment
domain.manager
```

Each name identifies one logical object. That object has one primary IID and
may advertise additional interface IIDs obtainable through `getInterface()`.
The same name must not refer to unrelated objects depending on which IID was
requested.

The initial interface operations are conceptually:

```text
resolve(name, out handle)                    -> primary interface
resolve_as(name, iid, out handle)            -> requested interface view
get_primary_iid(name, out uuid)              -> primary IID
get_advertised_iids(name, out array<uuid>)   -> discoverable IID set
list_bindings(out array<BindingInfo>)         -> visible names and primary IIDs
```

`resolve_as` must have the same result as resolving the named object and
calling `getInterface(iid)`, except that it may avoid an extra round trip.

The advertised IID list is discovery metadata, not authority and not a proof
that a future dynamic query must succeed. `resolve_as` remains authoritative.

The inherited `IGenericObject.getInterface(iid)` on the environment resolves
the environment's unique default binding/factory for that IID. If no default
exists it returns `FACET_NO_INTERFACE`. If configuration would create more
than one default for the same IID, environment construction fails rather than
choosing arbitrarily.

`IPOSIXView` is allowed to be a factory-backed default: asking the process
environment for that IID constructs the process-specific view using private
dependencies.

### 6.4 Binding visibility

Only visible bindings appear in lookup and enumeration results. Private
construction dependencies are held by the server-side environment object and
are absent from its public namespace.

This distinction is essential for pure POSIX processes. Their environment can
construct `IPOSIXView` using filesystem, stream, identity, clock, network, and
memory objects without making those objects directly discoverable.

Enumeration is optional authority. A restricted environment may allow lookup
of known names while denying `list_bindings`. Failure to enumerate must not
imply that no other names can be resolved.

### 6.5 Binding ownership and lifetime

The environment owns its server-side binding table for at least the lifetime
of the process. Resolving a binding returns an ordinary Facet handle according
to the libfacet ownership rules.

- A returned proxy may be freed without releasing its handle.
- A caller releases a handle explicitly when finished.
- If an existing exported server can be reused, the implementation may reuse
  that server endpoint/capability rather than spawning another server.
- Independent caller ownership must still be represented by an independently
  releasable handle or by an equivalent safe platform mechanism.
- Libfacet does not impose reference counting on the concrete application
  object. Concrete implementations determine their own object lifetimes.

Destroying a process environment revokes or releases its environment-owned
bindings. Handles deliberately delegated independently may outlive the
environment only when their exporting implementation supports that lifetime.

### 6.6 Standard binding profiles

A native process commonly receives:

- `stdin` as `IByteReader`;
- `stdout` as `IByteWriter`;
- `stderr` as `IByteWriter`;
- `logger` as `ILogger`;
- `memory.pages` as a process-bound `IPageAllocator`;
- optional native services explicitly selected by policy.

A hybrid process receives an `IPOSIXView` plus selected native bindings.

A pure POSIX process can obtain only `IPOSIXView`. Its standard descriptors
and POSIX dependencies are private to that view and must not appear as direct
Facet bindings.

## 7. Page allocation and process memory

### 7.1 Process-bound allocator

An `IPageAllocator` exported to a process is bound server-side to:

- that process's VSpace/address-space object;
- its memory quota;
- the parent resource allocator from which frames are drawn;
- an allocation ownership table;
- applicable page-size and mapping policy.

The process does not pass a VSpace handle when allocating. The server already
knows the target address space from the exported allocator instance.

The initial operations remain:

```text
get_page_size(out u64 page_size)
alloc(in u64 count, out local_ptr pages)
free(in u64 count, in u64 base)
```

`local_ptr` is deliberately permitted only as an output. In the seL4 server it
is encoded as a `uintptr_t` representing the virtual address mapped into the
caller. `free` accepts that address as an integer and validates it against the
allocator's ownership table.

### 7.2 Allocation requirements

Allocation must:

- reject zero count and multiplication/rounding overflow;
- enforce quota before allocating;
- choose a suitably aligned free virtual range in the target process;
- allocate/retype frames through the authorized resource manager;
- map every page with the configured rights;
- roll back all partial work on failure;
- record the complete allocation before returning;
- return the target process's virtual address, not dominit0's mapping.

Free must:

- require the exact base and count of an owned allocation in version 1;
- reject overlapping, partial, foreign, or already freed ranges;
- unmap the target VSpace;
- release frames and metadata according to parent policy;
- invalidate the ownership record atomically from the caller's perspective.

The allocator used by `dominit` initially backs its userspace heap after the
small static bump allocator is exhausted. `dominit` must not call seL4 APIs to
obtain or map those pages.

Pure POSIX applications do not receive `IPageAllocator`; their `brk`, `mmap`,
and `munmap` operations go through `IPOSIXView`.

## 8. Byte streams, terminals, and seats

### 8.1 Byte-stream interfaces

Standard I/O, pipes, terminal data, files, and sockets are byte-oriented.
Unicode decoding is a higher-level concern.

Use separate authority-bearing interfaces:

```text
IByteReader.read(maximum, out bytes)
IByteWriter.write(bytes, out written)
```

Version 1 semantics are:

- operations may return fewer bytes than requested;
- a successful read returning zero bytes means end-of-stream;
- a successful write may be partial;
- callers must retry partial writes when required;
- blocking behavior is determined by the concrete stream and later optional
  nonblocking interfaces;
- transferred buffers are length-delimited and subject to transport limits;
- read authority and write authority are independently delegable.

Bidirectional objects may expose both interfaces through `getInterface()`.
They are not collapsed into one interface because many processes should
possess only one direction.

### 8.2 Terminal interfaces

An `ITerminal` represents a terminal session. It may provide:

- an input `IByteReader`;
- an output `IByteWriter`;
- an optional `ITerminalControl` interface for terminal modes, dimensions,
  foreground process group, and hangup;
- administrative metadata not exposed to ordinary applications.

Processes normally receive only the reader/writer objects. Possession of
those streams does not grant authority to reassign the terminal, inspect
other sessions, or control the seat.

`stderr` may be a byte writer backed by a logger adapter. The application does
not need to know whether bytes go to a terminal, pipe, file, or logger.

### 8.3 Seats

An `ISeat` is an administrative collection of input devices, output devices,
and virtual terminals. Seat authority belongs to a seat manager or domain
manager, not to terminal applications.

Conventions for the first implementation are:

- `seat0` represents serial consoles;
- `seat0.ttys0` is the first serial terminal;
- additional serial ports may produce `ttys1`, `ttys2`, and so on;
- `seat1` represents the first local keyboard/display collection;
- `seat1.tty1`, `seat1.tty2`, and later terminals are local virtual terminals.

These are defaults, not globally privileged names. A nested domain may define
its own `seat0` using only devices delegated to it.

The seat manager controls active-terminal switching and input routing. A
terminal launch entry controls which process is attached to a terminal; the
seat itself does not decide whether that process is native, POSIX, or a VM.

### 8.4 Terminal launch entries

Each domain configuration may associate launch specifications with terminals.
A launch specification includes:

- terminal name;
- executable source;
- personality: `native`, `hybrid`, or `posix`;
- arguments;
- initial POSIX environment overrides;
- native process-environment bindings;
- credentials or login policy where applicable;
- resource limits;
- whether launch failure is fatal for the domain.

Typical Domain 0 policy may bind:

- `seat1.tty1` to `/FacetOS/FacetLogin` as a native process;
- `seat1.tty2` to `/bin/login` as a POSIX view of Domain 0;
- another terminal to a pure POSIX child domain;
- another terminal to a VM domain's console.

## 9. POSIX personality

### 9.1 POSIX view versus POSIX domain

A POSIX view runs POSIX software within an existing native domain. A pure
POSIX domain is a separate domain whose primary personality is POSIX and whose
first POSIX process is PID 1. A UNIX VM domain runs a real guest kernel and is
not the same as either.

The same ordinary POSIX executable should run under a native-domain POSIX
view or in a pure POSIX domain without being rebuilt.

### 9.2 IPOSIXView

`IPOSIXView` is the single public syscall-like ABI consumed by the FacetOS
POSIX libc. It is intentionally monolithic at the client boundary so libc can
obtain one process-specific object during startup and route its system calls
through it.

Internally, an `IPOSIXView` implementation is composed from narrower Facet
objects, including as available:

- filesystem namespace and open-file services;
- process creation and lifecycle services;
- credentials and identity services;
- signal delivery/state;
- clocks and timers;
- byte streams and terminal control;
- networking;
- process-bound memory services;
- random-data and system-information providers.

These dependencies are supplied by the server-side process environment
factory. They do not have to be visible to the client.

`IPOSIXView` owns or refers to per-process state:

- PID and parent relationship;
- credentials and groups;
- root and current directories;
- mount namespace;
- file-descriptor table;
- umask;
- signal dispositions and masks;
- process limits;
- controlling terminal and session/process-group state.

The exact syscall method list must be specified in a dedicated POSIX ABI
document before freezing its UUID. Until then `uuid auto` is appropriate.
All methods return `FacetResult` for transport/service status and carry POSIX
return values and `errno` explicitly as output data; transport failure must
not be mistaken for a POSIX error.

### 9.3 Pure, hybrid, and native visibility

- A native process receives selected direct Facet services and no POSIX view.
- A hybrid process receives `IPOSIXView` and selected direct Facet services.
- A pure POSIX process's environment exposes only `IPOSIXView`.

For a pure POSIX process, descriptors 0, 1, and 2 are installed from private
stream dependencies. The process cannot resolve those underlying stream
objects directly through `IProcessEnvironment`.

A hybrid process may receive direct streams and native services in addition
to its POSIX descriptors. This is deliberate and is what permits software to
combine normal POSIX APIs with Facet interfaces.

### 9.4 IPOSIXEnvironment

`IPOSIXEnvironment` is an immutable bootstrap representation of POSIX
environment key/value pairs. Its initial operations are conceptually:

```text
get(name, out present, out value)
snapshot(out array<EnvironmentEntry>)
```

Keys may not contain `=` or NUL. Values may not contain NUL. Duplicate keys
are rejected when constructing a snapshot. Ordering has no semantic meaning.

At process startup libc obtains the snapshot and constructs its normal local
`environ`. `getenv`, `setenv`, `unsetenv`, and `putenv` operate locally and do
not perform RPC.

Spawn and exec operations receive an explicit copied environment snapshot.
The parent library applies local changes before sending it. On successful
exec, the new image receives that snapshot; on failed exec, the calling
process's local environment is unchanged.

Configuration supplies defaults such as `PATH`, `HOME`, `USER`, `SHELL`,
locale variables, and `TERM`. Login programs replace or augment these values
after authentication.

### 9.5 Filesystem layout and identity

The conventional POSIX hierarchy remains normal: `/bin`, `/sbin`, `/usr`,
`/etc`, `/home`, and so on. Native executables stored in a filesystem use the
`/FacetOS` hierarchy, for example `/FacetOS/FacetLogin`.

A POSIX view may expose a synthetic, read-only `/etc` generated from FacetOS
configuration and device/identity services. Pure POSIX domains may instead
have an independent root filesystem and writable policy-selected portions.

Shared accounts and home directories are modeled using abstract services:

- an identity/account provider;
- a filesystem or file-service object;
- optional name-service and network transports.

NIS and NFS may implement or bridge these services, but are not fundamental
FacetOS object-model requirements. A VM receives NFS/NIS or equivalent access
only through an explicit export/bridge configured by an authorized parent.
Configuration chooses what is exported; possession of the bridge capability
determines whether the VM can actually use it.

### 9.6 Process operations

POSIX process behavior must preserve familiar semantics:

- `fork` creates a child process view with copied process state and a copied
  descriptor table referring to the appropriate shared open-file
  descriptions;
- `exec` replaces the image while retaining the POSIX process identity and
  applying POSIX descriptor/signal inheritance rules;
- PID values are meaningful only inside the POSIX personality/domain;
- signals are delivered through the personality service, not exposed as raw
  seL4 notifications to applications;
- `mmap`, `munmap`, and `brk` use private process-bound memory authority;
- credentials are fixed by the server-side view and cannot be forged in RPC
  arguments.

## 10. Domains and management

### 10.1 Domain identity and hierarchy

A domain is a collection of delegated resources and authority, not a process
or VSpace. It may contain native services, POSIX processes, drivers, child
domains, or a VM.

Domain IDs are unique only among siblings or within the manager's chosen
hierarchy. Domain 0 conventionally names the root of a hierarchy. Nested
FacetOS instances may each have their own Domain 0 without gaining authority
outside their parent delegation.

### 10.2 Domain manager authority

`IDomainManager` is supplied only to processes authorized to create or manage
domains. Domain 0's `dominit` receives it by default when root policy enables
management. A child may receive:

- no domain-management interface;
- a manager restricted to resources already delegated to that child;
- a proxy to a parent manager that evaluates each request.

The manager validates requested resources against policy and actual available
authority. It constructs the child's CSpace, VSpace, threads, resource
objects, environment, and root handle. It passes only the child's effective
configuration and delegated objects.

### 10.3 Domain personalities

The initial personality values mean:

- `native`: start `dominit` with `IDomainEnvironment`;
- `posix`: create a POSIX personality and start `/sbin/init` as PID 1;
- `vm`: start a configured VMM with delegated CPU, RAM, devices, and bridge
  services.

Personality does not imply authority. A VM configured to own a GPU receives
GPU resources only if its parent possesses and delegates them.

## 11. Device and service discovery

### 11.1 Registry purpose

The driver registry records factories, matching metadata, and produced
interfaces. It is not a global source of authority and need not contain every
object in the system.

Drivers should normally run in isolated processes. In-process drivers are
acceptable during bring-up but must use the same interfaces and be replaceable
without changing consumers.

### 11.2 Driver factories

A registered driver factory declares:

- `interested_in()`: input device/bus IIDs it can inspect or bind;
- `provides()`: IIDs instances may expose after binding;
- optional match data such as bus IDs, class codes, or compatibility strings;
- a probe priority and whether probing is destructive;
- the executable or service needed to instantiate it.

The device manager performs this sequence:

1. discover a device object from a bus or platform enumerator;
2. compare its advertised interfaces and immutable identifiers with factory
   matching metadata;
3. ask candidate factories to perform non-destructive probing where needed;
4. choose one according to explicit policy and probe result;
5. construct an isolated driver process/environment;
6. delegate only the selected device object and required resources;
7. register the interfaces actually exported by the resulting driver;
8. notify interested consumers.

Metadata may cause the manager to offer a handle to a driver. It never lets a
driver manufacture access to an undelegated device.

### 11.3 Buses and devices

An `IBusDriver` enumerates child device objects and reports additions and
removals. A bus device may itself expose several interfaces. For example, a
PS/2 bus can produce `IPS2Device` objects; a selected keyboard driver can
produce an `IPS2KeyboardDevice` and generic keyboard/input interfaces.

Seat construction depends on generic input/output interfaces where possible,
not directly on PS/2-specific types. Bus-specific interfaces remain available
to drivers that need them.

### 11.4 VM-backed services and drivers

A service implemented inside a VM is exported through a bridge that presents
ordinary Facet handles to the rest of the system. Consumers and the registry
must not need to know whether an implementation is native, remote, or hosted
inside another OS.

The bridge is responsible for translating lifecycle, cancellation, errors,
and data transfer. Configuration explicitly selects resources and services
exported to or imported from the VM. No ambient network reachability is
treated as equivalent to a Facet capability.

## 12. Boot and launch sequence

The root-domain sequence is normative:

1. Enter `dominit0` with the kernel-provided root-task environment.
2. Start emergency character output and the allocation-free early log buffer.
3. Read and validate the seL4 BootInfo and preserved multiboot-module
   descriptors.
4. Initialize the bootstrap allocator, allocman, VKA, the root VSpace, and the
   full heap.
5. Initialize `libfacet-platform-sel4` for exporting and invoking objects.
6. Locate exactly one `facet.toml` module and parse it into a temporary syntax
   tree without starting configured services.
7. Validate the complete schema and all boot-module references.
8. Construct configured logging sinks, logging policy, and the root logger.
9. Transfer retained early records to the full logger and switch the logging
   facade.
10. Inventory platform resources and construct generic resource objects.
11. Construct the root domain's effective read-only configuration.
12. Create a process-bound `IPageAllocator` for `dominit`.
13. Build `dominit`'s `IProcessEnvironment`, including `logger`,
    `memory.pages`, configured streams, and any authorized manager services.
14. Build the root logical object exposing both `IDomainEnvironment` and the
    bootstrap `IProcessEnvironment` view.
15. Export the root object and mint/copy its platform capability into the
    child CSpace with call authority but no server receive authority.
16. Load the configured `dominit` ELF, pass the bootstrap handle and libfacet
    receive-slot information through the platform startup ABI, and start it.
17. In `dominit`, initialize only the selected libfacet platform client,
    convert the startup integer into an `IGenericObject`, and obtain
    `IDomainEnvironment` and `IProcessEnvironment`.
18. Switch `dominit` to its `ILogger`, then switch its heap from the static bump
    allocator to `IPageAllocator`-backed memory.
19. Start configured seats, drivers, services, terminal programs, and child
    domains in dependency order.
20. Enter the domain-management/event loop.

`dominit` source after its tiny platform bootstrap must not call seL4 APIs.
The same portable code must be usable with a future
`libfacet-platform-linux` and a Linux bootstrap adapter that supplies an
equivalent root object.

## 13. Failure handling

### 13.1 Fatal root failures

The following are fatal during root bootstrap:

- inability to initialize emergency output when no alternative exists;
- malformed or unsupported required configuration;
- duplicate/missing required configuration module;
- failure to initialize core memory/resource management;
- failure to initialize libfacet transport;
- failure to construct a required log sink or root logger;
- failure to locate or load the required root bootstrap executable;
- failure to construct/export the root environment;
- failure to create the root bootstrap process.

A fatal path emits through the most reliable available sink and halts without
continuing in a partially initialized authority state.

### 13.2 Optional components

Drivers, terminals, sinks, services, and child domains may be marked optional.
Failure of an optional component is recorded and dependency consumers are
either skipped or degraded according to their own `required` flag.

Required dependencies are transitive. If an optional driver is absent but a
required seat depends on its keyboard interface, seat initialization failure
is fatal for the containing required launch unit.

### 13.3 Cleanup and rollback

Every multi-step resource operation must define rollback. In particular,
process creation, page allocation, capability installation, driver launch,
and interface export must release all successfully created intermediate
objects if a later step fails.

An exported object must not become discoverable until its initialization and
binding table are complete.

### 13.4 Diagnostics

Configuration diagnostics include source location and a stable category:

- syntax;
- unsupported version;
- schema/type;
- duplicate declaration;
- unresolved reference;
- dependency cycle;
- unavailable authority/resource;
- runtime initialization failure.

Secrets must not be written to logs. The root configuration should not contain
long-lived plaintext credentials; it should refer to credential providers or
protected objects instead.

## 14. Concurrency, lifecycle, and revocation

Version 1 inherits libfacet's serialized-per-exported-object server model.
Object implementations must avoid synchronous dependency cycles that require
reentrant calls into the same busy export.

Environment factories, logging sinks, and POSIX views must not hold a global
dominit0 lock across an RPC call into another process. Lock ordering must be
documented when a service has more than one lock.

Concrete object lifecycle is implementation-defined; libfacet does not impose
application-object reference counts. Platform handles and proxy allocations
still obey explicit clone, free, release, export, and unexport operations.

Parent managers must retain enough provenance to revoke:

- all bindings belonging to a terminated process;
- all resources delegated to a destroyed domain;
- driver access after device removal;
- terminal stream access after session teardown;
- VM bridge exports when the VM stops.

Revocation must not depend on configuration names remaining present.

## 15. Interface and IDL requirements

The implementation will require new or revised IDLs for at least:

- `ILogger`;
- `ILoggerSink`;
- `ILoggingConfig`;
- `IDomainEnvironment`;
- `IProcessEnvironment`;
- `IPOSIXEnvironment`;
- `IPOSIXView`;
- `IByteReader`;
- `IByteWriter`;
- `ITerminal`;
- `ITerminalControl`;
- `ISeat`;
- `IDomainManager`;
- `IProcessManager`;
- driver registry/factory/bus interfaces.

The following rules apply:

- unstable interfaces use `uuid auto` while iterating;
- an interface receives an explicit UUID before persistent use or ABI freeze;
- declaration order determines method IDs;
- incompatible changes require a new UUID;
- `requires` means the logical object promises the requested runtime interface
  view; it is not inheritance or a type import;
- shared enum/struct declarations use a future type-only IDL import;
- object returns use `FacetResult` plus `out handle`;
- local process addresses use `local_ptr` only where explicitly allowed;
- no generated interface contains seL4 types;
- all arrays and strings are length-delimited and resource-limited;
- handles travel as platform authority attachments, never serialized pointer
  values.

Before adding shared logging, stream, environment, and POSIX schema types,
facet-idlc must implement type-only imports with cycle detection, duplicate
type detection, deterministic include generation, and no runtime
`getInterface()` implications.

## 16. Testing requirements

### 16.1 Configuration tests

- Parse the documented minimal configuration.
- Reject malformed TOML, duplicate keys, overflow, excessive nesting, and
  oversized strings/arrays.
- Reject duplicate module aliases, domains, seats, terminals, and bindings.
- Reject unresolved required references and dependency cycles.
- Confirm an optional unavailable service is skipped with a diagnostic.
- Confirm a configuration declaration cannot create a handle the parent does
  not possess.

### 16.2 Logging tests

- Log before dynamic allocation and preserve the record.
- Transition to the configured logger without duplicating emergency output.
- Enforce domain, process, component, and sink levels server-side.
- Continue through failure of an optional sink.
- Avoid recursion when the logger or sink reports an error.
- Prove `dominit` logs without direct seL4 calls.

### 16.3 Environment and authority tests

- Resolve by name, by name and IID, and by default IID.
- Return the configured primary IID and advertised IID set.
- Reject duplicate defaults for one IID.
- Ensure a pure POSIX environment enumerates and resolves only `IPOSIXView`.
- Ensure private POSIX construction dependencies cannot be resolved by name.
- Ensure ordinary processes cannot obtain `IDomainEnvironment`.
- Verify returned proxies/handles follow independent lifetime rules.
- Revoke all process-owned bindings on process teardown.

### 16.4 Memory tests

- Allocate pages into the child's VSpace and read/write them from the child.
- Confirm the returned address is valid in the child and not accidentally the
  server's address.
- Reject zero, overflow, over-quota, foreign, partial, and double frees.
- Roll back mappings and frames after every injected partial failure.
- Grow `dominit` beyond its static bump heap using only `IPageAllocator`.

### 16.5 Personality and terminal tests

- Run the same POSIX binary in a native-domain POSIX view and a pure POSIX
  domain.
- Run a hybrid binary that uses both libc and an explicitly delegated Facet
  interface.
- Confirm descriptors 0/1/2 work while direct streams remain hidden in pure
  POSIX mode.
- Snapshot, modify locally, inherit, and replace a POSIX environment across
  spawn/exec.
- Bind native and POSIX login programs to distinct terminals on one seat.
- Confirm terminal stream authority does not permit seat reassignment.

### 16.6 Driver and VM tests

- Match a driver by accepted IID and immutable device identifiers.
- Confirm an unselected driver receives no device handle.
- Remove a device and revoke its driver's delegated objects.
- Consume the same provided interface from an in-process, isolated native,
  and VM-bridged implementation.
- Confirm NFS/NIS or equivalent VM export is unavailable unless explicitly
  configured and delegated.

## 17. Milestones

### Milestone 1: Configuration parser and boot integration

- Implement the bounded TOML subset and source-located diagnostics.
- Locate and preserve `facet.toml` as a multiboot module.
- Parse and validate `[facet]`, `[[modules]]`, `[logging]`, and `[root]`.
- Keep all existing fallback behavior behind an explicit development option.
- Add parser, schema, malformed-input, and module-selection tests.

Completion criterion: dominit0 boots from a minimal valid configuration and
halts with a precise emergency diagnostic for every invalid required input.

### Milestone 2: Unified logging

- Add type-only IDL imports needed by shared logging types.
- Define and generate `ILogger`, `ILoggerSink`, and revised
  `ILoggingConfig` interfaces.
- Implement the generic logger/router and seL4 debug sink.
- Import the early klog buffer and switch to configured logging.
- Preserve a recursion-safe emergency path.

Completion criterion: all post-bootstrap dominit0 output flows through the
generic logger, with configured filtering and no duplicate early records.

### Milestone 3: Process and domain environments

- Replace the current mutable `IDomainEnvironment` design.
- Define and implement read-only `IDomainEnvironment` and
  `IProcessEnvironment` objects.
- Implement named lookup, typed lookup, primary IID inspection, restricted
  enumeration, and default-IID construction.
- Export one root logical object with domain and bootstrap-process views.

Completion criterion: dominit starts from an `IDomainEnvironment`, obtains its
process environment, resolves `logger`, and emits a message without knowing
about seL4.

### Milestone 4: Child-bound page allocation and dominit heap

- Implement allocation ownership and quotas per child VSpace.
- Correctly map pages into dominit and return its local address.
- Add transactional rollback and exact-allocation free validation.
- Switch dominit from the static bump allocator to allocator-backed heap
  growth as soon as the interface is available.

Completion criterion: dominit allocates beyond its bootstrap heap without any
direct seL4 API use and all negative allocation tests pass.

### Milestone 5: Byte streams and terminal bootstrap

- Define `IByteReader`, `IByteWriter`, `ITerminal`, and
  `ITerminalControl`.
- Implement serial debug/console stream adapters.
- Add `seat0`, serial terminal discovery, and configured process stdio.
- Add logger-backed `stderr` as an interchangeable byte writer.

Completion criterion: a native process receives working named stdin, stdout,
and stderr bindings with no seat-management authority.

### Milestone 6: Native process launcher and seat1

- Define the process-launch service and launch-spec representation.
- Implement configured native executable/module loading.
- Add local keyboard/display seat management and virtual-terminal switching.
- Launch `/FacetOS/FacetLogin` on a configured terminal.

Completion criterion: configuration controls native programs on multiple
terminals, and changing the active terminal changes routing without changing
process authority.

### Milestone 7: Minimal POSIX view

- Specify and generate the first frozen subset of `IPOSIXView`.
- Implement `IPOSIXEnvironment` snapshot semantics.
- Implement descriptors 0/1/2, basic file operations, process exit, clocks,
  and allocator-backed `brk`/`mmap` sufficient for a small libc program.
- Implement pure and hybrid process-environment profiles.

Completion criterion: one unmodified POSIX test binary runs both as a POSIX
view in Domain 0 and inside a pure POSIX test domain; a hybrid test can also
invoke a delegated Facet interface.

### Milestone 8: POSIX login and filesystem policy

- Add configured root filesystem and synthetic read-only `/etc` support.
- Launch `/bin/login` or a simple getty on a configured terminal.
- Add identity-provider and home-filesystem abstractions.
- Implement local providers before network-backed adapters.

Completion criterion: a user can authenticate through a POSIX terminal and
receive the configured home, environment, credentials, and filesystem view.

### Milestone 9: Child domains

- Define and implement `IDomainManager` and child effective-configuration
  construction.
- Delegate bounded resources and filtered process/domain environments.
- Start native and pure POSIX child domains without passing the root config.
- Implement domain teardown and revocation.

Completion criterion: Domain 0 starts a child domain, the child can use only
delegated services, and teardown revokes all child resources.

### Milestone 10: Driver registry and device manager

- Define driver factory, registry, bus, and device discovery interfaces.
- Load isolated drivers from configured modules/files.
- Implement matching, probe ordering, restricted resource delegation, and
  hot-removal cleanup.
- Convert seat device discovery to consume generic driver outputs.

Completion criterion: a keyboard path is assembled through bus/device/driver
interfaces and removing the device safely tears down its authority.

### Milestone 11: VM domains and service bridges

- Add VM-domain configuration and VMM launch support.
- Delegate explicit CPU, RAM, device, terminal, and network resources.
- Export a VM-hosted service through an ordinary Facet interface bridge.
- Add explicit shared identity/filesystem export policy suitable for later
  NFS/NIS adapters.

Completion criterion: a VM domain provides or consumes one bridged service
without consumers depending on the fact that its implementation is in a VM.

### Milestone 12: ABI stabilization and portability

- Assign explicit UUIDs to interfaces whose ABIs are ready to persist.
- Document compatible evolution rules and all remaining resource limits.
- Implement a Linux bootstrap/platform adapter for the portable dominit path.
- Run common configuration, environment, logging, and object tests on seL4
  and Linux.

Completion criterion: the same dominit core builds and performs its initial
configuration/environment/logging flow on both platform implementations, with
no platform-specific APIs in portable source.
