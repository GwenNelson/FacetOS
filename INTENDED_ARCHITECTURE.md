# FacetOS Intended Architecture

## 1. Purpose and design principles

FacetOS is an operating system built on top of a microkernel platform. The
system is divided into isolated domains which define boundaries of authority,
resources and system visibility.

There are two broad classes of domain:

- The **system domain**, containing the microkernel root task (`dominit0`) and
  system drivers. It begins with the authority made available by the underlying
  microkernel and is responsible for constructing and delegating the initial
  FacetOS environment.

- **Userland domains**, numbered from domain 0 upwards. There is no intentional
  architectural limit on their number beyond available system resources.

Every userland domain has a domain ID, name, configuration and initial process.

A domain may be configured as a normal FacetOS domain, a pure POSIX domain, or
for a specialised purpose such as containing a virtual machine. These are
properties of how the domain's initial environment is constructed rather than
different kinds of process enforced by the kernel.

### 1.1 Native and POSIX programs

There is deliberately **no native or POSIX process personality stored by
FacetOS**.

A process is simply a process containing an executable and an
`IProcessEnvironment`.

FacetOS does not mark an executable as native or POSIX, nor does
`IProcessManager` create fundamentally different native and POSIX process
types.

The distinction arises exclusively from how the program accesses the system:

- a **native FacetOS program** obtains and invokes the underlying Facet
  interfaces made available through its `IProcessEnvironment`;

- a **POSIX program** is linked against `libc-facet-posix`, which implements
  its POSIX operations by invoking the `IPOSIXView` supplied through that same
  `IProcessEnvironment`.

`IPOSIXView` may be present in the environment of native programs as well.
Its presence does not make a process POSIX.

A native program can therefore start a POSIX program without changing process
type or entering a different execution personality. It constructs the child's
`IProcessEnvironment`, includes the appropriate `IPOSIXView`, and starts the
executable normally.

Likewise, a program such as `FacetShell` is capable of starting either native
FacetOS executables or POSIX executables. The difference becomes apparent only
when the child executable begins using the interfaces provided to it.

This distinction is fundamental:

> **Native versus POSIX describes how program code accesses FacetOS services,
> not a property, flag, execution mode or security personality attached to a
> process.**

### 1.2 Facet authority

FacetOS exposes system services and resources through a native object-based
interface system known as Facet.

Facet objects may be local to a process or exported for use through Facet RPC.

Processes obtain authority to access system services and resources through
Facet interfaces. Except for explicitly platform-specific components,
underlying microkernel capabilities and other platform-specific authority
mechanisms are not exposed directly.

Any process may both import and export Facet objects.

Exporting objects is not an exceptional operation reserved for servers.
Numerous Facet APIs rely upon callers exporting objects and passing the
resulting handles to other objects.

### 1.3 Platform independence

The implementation maintains a strict separation between FacetOS semantics and
the mechanisms of the underlying platform.

Portable components depend only upon Facet interfaces and portable Facet
libraries.

Platform-specific behaviour is isolated behind explicit platform abstraction
layers so that, wherever practical, a component can be built for another
supported platform by selecting a different platform implementation rather
than modifying its portable source.

A particularly strict source-tree rule applies:

> **For any component `$component`, references to seL4 are permitted only
> beneath `src/$component/platform/sel4/`.**

Outside such platform-specific directories:

- seL4 headers must not be included;
- seL4 types must not appear;
- seL4 functions must not be called;
- seL4 constants must not be referenced;
- seL4 capability representations must not be assumed;
- implementation decisions must not depend upon seL4 behaviour.

Any such dependency outside the appropriate platform abstraction directory is
an architectural bug and must be moved behind that component's platform
abstraction layer.

The same principle applies to other platform implementations.

Portable libraries used by FacetOS should obtain system functionality through
Facet interfaces rather than POSIX APIs or direct platform calls.

---

## 2. seL4 and kernel responsibilities

The current primary microkernel platform for FacetOS is seL4, chosen in part
for its capability model, IPC facilities and formal verification properties.

seL4 provides low-level mechanisms including:

- address spaces;
- threads;
- capabilities;
- paging;
- IPC;
- hardware I/O authority;
- IRQs;
- kernel object creation.

FacetOS follows the principle that the microkernel provides mechanism rather
than FacetOS policy.

seL4 has no knowledge of FacetOS domains, Facet processes, Facet objects,
Facet interfaces, filesystem views, POSIX views or the higher-level authority
model constructed from them.

On seL4, `dominit0` executes as the root userspace task and initially receives
the raw authority from which the FacetOS environment is constructed.

`libfacet-platform-sel4` maps the portable Facet RPC model onto seL4 IPC and
capability transfer.

Low-level Facet handle transport and capability manipulation belong within
`libfacet-platform-sel4`.

Address-space construction and other process-creation operations required by
`dominit0` belong within:

```text
src/dominit0/platform/sel4/
```

Hardware drivers execute as independent userspace processes within the system
domain rather than inside seL4.

Where a driver requires unavoidable direct platform interaction, that code
must likewise be isolated beneath:

```text
src/<driver>/platform/sel4/
```

The portable portion of the driver must contain no seL4 dependencies.

Processes outside the system domain do not manipulate vSpaces, CSpaces or raw
seL4 authority in order to create processes. They invoke the
domain-specific `IProcessManager` provided to them.

Domains are intended to be strongly isolated.

Processes in different domains are not currently provided with a general
mechanism for directly exporting interfaces to one another. A future version
of FacetOS may deliberately support controlled cross-domain interface
delegation, but this is not presently part of the intended interface.

Domains may nevertheless use resources deliberately shared by system
configuration, such as access to the same underlying filesystem service.
Such sharing is explicit and does not imply general authority to interact with
processes in the other domain.

---

## 3. `dominit0`

`dominit0` is the initial privileged FacetOS userspace process and the root of
system-wide resource management and authority delegation.

On seL4, it is the initial/root userspace task and receives the initial
capabilities supplied by the microkernel, including untyped memory and
available hardware authority.

`dominit0` converts these platform resources into the higher-level Facet
objects from which the rest of FacetOS is constructed.

Raw seL4 capabilities must not normally escape `dominit0`, drivers or
seL4-specific platform abstraction code.

### 3.1 Global responsibilities

`dominit0` is responsible for system-wide facilities including:

- physical resource management;
- address-space construction;
- creation of processes and threads;
- hardware discovery;
- driver startup;
- creation of domains;
- construction of each domain's initial authority;
- globally managed hardware resources;
- construction of the initial seat and terminal environment.

Where less privileged components require access to these facilities,
`dominit0` exports Facet interfaces representing restricted authority rather
than exposing its underlying platform authority.

### 3.2 Domain construction

For every configured userland domain, `dominit0` constructs an
`IDomainEnvironment`.

The `IDomainEnvironment` contains the initial Facet interfaces exported into
that domain.

`dominit0` then creates the domain's `dominit` process and provides that
environment to it.

The same general pattern is subsequently repeated at the process level:

```text
dominit0
   |
   | constructs IDomainEnvironment
   v
dominit
   |
   | constructs IProcessEnvironment
   v
child process
```

This establishes an explicit chain of authority delegation.

### 3.3 Domain-specific process managers

Each domain receives its own `IProcessManager`.

The `IProcessManager` supplied to a domain is restricted to creating processes
within that domain.

It cannot use its process-creation authority to escape the domain or give a
new process interfaces which have not been made available to that domain.

`dominit` may pass this `IProcessManager` onward to processes within the
domain. Shells and other programs may therefore create their own children
without requiring unrestricted access to the system-wide process creator.

The implementation behind the domain-specific `IProcessManager` ultimately
requests the platform-specific process construction performed by `dominit0`,
while enforcing the authority boundary associated with that domain.

---

## 4. `dominit`

Every userland domain begins with an instance of `dominit`.

`dominit` receives an `IDomainEnvironment` constructed by `dominit0`.

It uses the authority available through that environment to construct and
start the initial processes belonging to the domain.

`dominit` is intended to be platform independent.

It must not contain direct seL4 dependencies. Any unavoidable platform-specific
implementation associated with the component must reside exclusively beneath:

```text
src/dominit/platform/<platform>/
```

### 4.1 Process environments

When `dominit` creates a process it constructs an `IProcessEnvironment`
containing the interfaces which that particular process is permitted to
access.

This normally includes the domain's restricted `IProcessManager`, allowing the
process to create children within the same domain.

It may additionally contain interfaces for:

- filesystem access;
- terminal access;
- memory allocation;
- `IPOSIXView`;
- authentication;
- other system or application services available within the domain.

The process's parent determines which of its available interfaces are
delegated into the child's `IProcessEnvironment`.

This is analogous to the way `dominit0` constructs an
`IDomainEnvironment` for `dominit`.

The resulting hierarchy is:

```text
system authority
      |
      v
dominit0
      |
      | IDomainEnvironment
      v
dominit
      |
      | IProcessEnvironment
      v
process
      |
      | IProcessEnvironment
      v
child process
```

At every stage, the creator may delegate authority it possesses but cannot
create authority absent from its own environment.

### 4.2 Native and POSIX execution

`dominit` does not request that `IProcessManager` create a "native process" or
a "POSIX process".

It simply creates a process with an executable and an
`IProcessEnvironment`.

Whether that executable subsequently behaves as a native or POSIX program
depends exclusively upon the APIs it uses.

---

## 5. Domains

A FacetOS domain is an isolation and authority boundary containing one or more
processes.

A domain determines which resources may be exported into the process
environments constructed within it.

A process belongs to one domain.

Processes in the same domain do not necessarily possess identical authority:
each receives its own `IProcessEnvironment`.

### 5.1 Domain isolation

Domains are intended to be substantially isolated from one another.

At present, general process-to-process interface delegation across domain
boundaries is intentionally unsupported.

A future system may provide controlled mechanisms for a process in one domain
to export an interface into another domain, but such a mechanism must be
explicit and must preserve the authority model.

Multiple domains may be configured to receive authority to the same underlying
resource, such as a filesystem. This represents explicitly shared authority
and does not weaken the isolation of unrelated resources.

### 5.2 Nested domain-like environments

FacetOS does not presently define or require a formal subdomain mechanism.

However, the Facet object model is deliberately general-purpose.

Nothing fundamentally prevents a process from running another copy or modified
version of `dominit`, implementing compatible interfaces itself, or
constructing an environment resembling another domain inside the authority it
already possesses.

This is neither a special supported security mechanism nor something FacetOS
needs to prohibit.

Most importantly:

> Constructing a nested `dominit` or imitating domain-management interfaces
> cannot create new authority.

Such an environment can only redistribute, proxy, restrict or emulate
authority already available to the process constructing it.

---

## 6. Process creation

Process creation is performed through `IProcessManager`.

Each domain receives a unique `IProcessManager` whose authority is restricted
to that domain.

The same process manager may be delegated by `dominit` to processes inside the
domain and subsequently to their children.

### 6.1 Generic creation sequence

At the architectural level, creating a process consists of:

1. selecting an executable;
2. constructing an `IProcessEnvironment`;
3. selecting which interfaces available to the parent should be exported into
   that environment;
4. exporting the `IProcessEnvironment`;
5. invoking the domain's `IProcessManager`;
6. having the platform process implementation construct the process;
7. supplying the root process-environment handle through the startup ABI;
8. starting execution.

The parent therefore controls the child's initial authority.

The domain-specific `IProcessManager` additionally ensures that the child
cannot receive authority which is not available within that domain.

### 6.2 Process inheritance

FacetOS does not define ambient inheritance of every object available to a
parent.

Interfaces are explicitly placed into the child's `IProcessEnvironment`.

In normal operation a parent will commonly propagate interfaces such as:

- the domain's `IProcessManager`;
- an `IPOSIXView`;
- filesystem objects;
- terminal objects;
- other resources intended to remain available to descendants.

This allows facilities such as `FacetShell` to launch arbitrary native and
POSIX programs using the same process-creation mechanism.

---

## 7. Authority delegation

FacetOS expresses application-level authority through Facet interfaces.

Possession of an interface permits the holder to invoke the operations exposed
by that interface.

Applications therefore receive interfaces rather than ambient system-wide
privileges.

Authority flows through explicit construction of object environments:

```text
dominit0
    |
    | selected authority
    v
IDomainEnvironment
    |
    v
dominit
    |
    | selected authority
    v
IProcessEnvironment
    |
    v
process
```

A process creating another process repeats the final step.

### 7.1 Attenuation

A process or system component may export an object which presents a restricted
view of more powerful authority it possesses.

For example, a domain-specific page allocator may ultimately obtain memory
from a system-wide allocator while enforcing a domain-specific allocation
limit.

The consumer receives authority only to the restricted object.

### 7.2 Export as delegation

Any process may export Facet objects.

This is an ordinary part of Facet programming and is required by many APIs.

An API may require its caller to export a callback, service object, stream,
filesystem object or other interface and pass the resulting `FacetHandle` as
part of an operation.

Exporting an object therefore does not imply that the exporting process is a
privileged system service.

---

## 8. Facet objects, exports, handles and proxies

Facet is the native object/interface mechanism used throughout FacetOS.

Interfaces are defined using Facet IDL and assigned UUIDs. `facet-idlc`
generates the C interface definitions and RPC support required to implement
and consume them.

### 8.1 Interface encapsulation

A Facet interface must be used exclusively through its defined accessors and
methods.

This rule applies even when the object is known to be local.

Code must not manipulate the implementation fields or semantic properties of
an object directly merely because it currently has access to its local C
representation.

For example, if an interface provides:

```text
get_foo()
set_foo()
```

consumers must use those accessors rather than directly changing the storage
currently used to implement `foo`.

This is a fundamental portability requirement because the same interface
instance may later be replaced by a proxy.

Code which relies upon direct access to implementation state would then behave
differently depending upon whether the object happened to be local or remote.

The invariant is therefore:

> **Anything observable through a Facet interface must be manipulated through
> that interface, regardless of whether the current object is local or a
> proxy.**

### 8.2 Local objects

A local Facet object is represented by an IDL-generated C interface structure
containing the appropriate method implementations and implementation context.

Calls to its methods may therefore execute as ordinary local C function calls.

This is an implementation optimisation and must not alter the semantics of the
interface.

### 8.3 Exported objects

Any process may export a local Facet object.

Exporting creates the platform-specific servicing machinery required for
another task to invoke that object and returns a `FacetHandle`.

Conceptually:

```text
local object
     |
     v
export
     |
     v
platform servicing machinery
     |
     v
FacetHandle
```

### 8.4 `FacetHandle`

A `FacetHandle` represents an exported Facet object.

Although the public Facet representation is portable, its usable underlying
representation is task/platform specific.

A raw handle received by one task must therefore not be assumed to be directly
usable by another task.

Any process may receive/import handles and may export its own objects to
produce new handles.

### 8.5 Proxies

A process receiving a `FacetHandle` constructs a local proxy using
`libfacet_proxy_from()`.

```text
FacetHandle
     |
     v
libfacet_proxy_from()
     |
     v
local proxy
     |
     v
Facet method calls
     |
     v
platform RPC
```

The proxy implements the same Facet interface as the remote object.

To the consuming code, operations on the proxy therefore follow the same
interface contract as operations on a local object.

---

## 9. Handle transfer and re-export

Facet RPC permits exported object references to be transferred between tasks.

A crucial distinction exists between **transferring a handle into a task** and
**re-exporting the resulting interface from that task**.

`FacetHandle`s are task-specific.

Suppose process A exports object X to process B:

```text
A:X
 |
 | export/transfer
 v
B:FacetHandle
 |
 v
B:proxy(X)
```

B now possesses a local proxy implementing X's interface.

If B subsequently wants to make that interface available to C, the ordinary
operation is for B to export **its local proxy object**:

```text
A:X
 |
 v
B:proxy(X)
 |
 | B exports proxy
 v
B-owned exported object
 |
 | transfer
 v
C:proxy
```

Calls from C therefore travel:

```text
C -> B proxy/export -> A
```

B remains part of the authority and lifetime chain.

B must not normally take the raw task-specific `FacetHandle` received from A
and attempt to hand that unchanged representation to C.

Doing so bypasses the task-specific export/import semantics and can produce
invalid or ambiguous platform behaviour.

Consequently, if B terminates, an object which B re-exported in this manner
ordinarily ceases to be available to C even if the original implementation in
A remains alive.

If direct A-to-C delegation is desired, it must be performed through an
explicit mechanism capable of exporting/transferring A's object appropriately
into C rather than by copying B's raw handle representation.

---

## 10. Filesystem and POSIX views

FacetOS native filesystem authority is expressed through Facet filesystem
interfaces such as `IFileStore`.

There is no requirement for every domain or process to observe one global
filesystem namespace.

### 10.1 `IPOSIXView` is process-specific

`IPOSIXView` is a per-process interface.

It represents the POSIX view made available to that process and is normally
included in its `IProcessEnvironment`.

A domain therefore does not itself intrinsically "have an `IPOSIXView`".
Rather, `dominit` constructs an initial `IPOSIXView` and places it into the
environment of the domain's initial process.

That process will normally propagate the same view, or an appropriately
derived view, into its children.

An `IPOSIXView` operates using the underlying Facet interfaces exported into
the relevant environment.

### 10.2 Normal native domains

A normal FacetOS domain may contain an `IPOSIXView` even if its initial and
most common programs are native FacetOS applications.

The normal POSIX filesystem view in such a domain is restricted to the
domain's `/posix` hierarchy.

Conceptually this behaves similarly to a chroot:

```text
native domain view

/
├── Apps
├── ...
└── posix
    ├── bin
    ├── home
    ├── etc
    └── ...
```

A POSIX program using its `IPOSIXView` observes the contents beneath
`/posix` as its POSIX root:

```text
IPOSIXView

/
├── bin
├── home
├── etc
└── ...
```

### 10.3 Synthetic `/etc`

In a normal domain, `/posix/etc` is provided by a synthetic read-only
`IFileStore` mounted into the native VFS.

Its contents exist at runtime and provide the conventional files required by
POSIX software, including where applicable:

```text
/etc/passwd
/etc/shadow
/etc/fstab
```

These files represent the domain's configured FacetOS environment rather than
requiring the native filesystem to store conventional UNIX account databases.

Through `IPOSIXView`, the same store appears simply as `/etc`.

### 10.4 Pure POSIX domains

A pure POSIX domain differs primarily in how its initial environment and
filesystem view are constructed.

Its initial process is the configured POSIX PID 1, initially `/bin/login` in
the intended configuration.

Its `IPOSIXView` does **not** use the special `/posix` root and synthetic
`/posix/etc` arrangement used by a normal native domain.

Instead, the pure POSIX domain is given its own real filesystem mounted as its
root:

```text
/
├── bin
├── etc
├── home
├── dev
└── ...
```

Its `/etc` is therefore the real `/etc` belonging to that filesystem.

The `IPOSIXView` itself continues to implement operations using the Facet
interfaces exported to the domain's `dominit`.

A pure POSIX domain is therefore not implemented by a special POSIX process
type.

The difference is principally:

```text
normal domain:
    initial program = configured native environment
    IPOSIXView root = /posix
    /etc = synthetic runtime IFileStore

pure POSIX domain:
    initial program = POSIX PID 1 (/bin/login)
    IPOSIXView root = domain's real filesystem /
    /etc = filesystem's real /etc
```

---

## 11. Seats and terminals

A terminal is a logical execution environment associated with the appropriate
Facet terminal and seat interfaces.

A terminal is not intrinsically native or POSIX.

The program started on that terminal determines how subsequent programs
normally access the system.

For example, a terminal may be configured to start:

```text
FacetLogin
```

which starts a native FacetOS session, or:

```text
/bin/login
```

which is linked against `libc-facet-posix` and therefore accesses the system
through its `IPOSIXView`.

The latter terminal can consequently be described as presenting a POSIX
environment, but this results from the executable running on it rather than
from a POSIX flag attached to the terminal itself.

A POSIX `/bin/login` normally starts another POSIX executable, which propagates
the relevant `IPOSIXView` into its children.

Likewise, `FacetShell` can start either native or POSIX programs because both
are ordinary processes constructed through the same `IProcessManager`.

---

## 12. Native and POSIX userland

There is no kernel or process-manager distinction between native and POSIX
executables.

### 12.1 Native programs

A native program accesses the system by obtaining Facet interfaces from its
`IProcessEnvironment` and invoking those interfaces directly.

For example, it may use native filesystem, process, terminal or service
interfaces without translating those operations into POSIX calls.

The fact that its `IProcessEnvironment` may also contain an `IPOSIXView` is
irrelevant unless some code actually uses that interface.

### 12.2 POSIX programs

A POSIX program is ordinary executable code linked against
`libc-facet-posix`.

Its conventional POSIX calls are translated by that libc into operations on
the `IPOSIXView` provided through its `IProcessEnvironment`.

Thus:

```text
POSIX program
     |
     | open(), getpwnam(), etc.
     v
libc-facet-posix
     |
     v
IPOSIXView
     |
     v
underlying Facet interfaces
```

FacetOS does not need to know that the executable is "a POSIX executable".

There is no executable classification mechanism, process-profile flag or
POSIX/native marker involved.

### 12.3 Mixed execution

Native and POSIX programs may therefore exist in the same domain and in the
same process ancestry.

For example:

```text
dominit
   |
   v
FacetLogin
   |
   v
FacetShell
   |          \
   |           \
   v            v
native app    POSIX app
                  |
                  v
             POSIX child
```

All are created using the same `IProcessManager`.

The distinction is solely which Facet interfaces the executable chooses to use
directly or indirectly.

---

## 13. Pure POSIX domains

A pure POSIX domain is an ordinary FacetOS domain whose initial environment is
constructed to present a conventional POSIX system from the beginning.

It does not introduce a different process personality or execution mechanism.

Its defining characteristics are:

- its initial process is its POSIX PID 1, initially `/bin/login` in the
  intended configuration;
- its `IPOSIXView` uses the domain's own filesystem as `/`;
- it has a real domain-specific `/etc`;
- assigned Facet resources are presented through appropriate POSIX names and
  APIs.

The underlying implementation remains Facet-based.

`IPOSIXView` performs its work through the Facet interfaces available from the
domain environment.

Thus the difference between a normal domain and a pure POSIX domain is
primarily **the initial namespace and process environment constructed by
`dominit`**, not the kind of process the microkernel executes.

---

## 14. Object and process lifetime

### 14.1 Export ownership

An exported object belongs to the task which exported it.

A `FacetHandle` referring to that export is meaningful within the
platform/task context into which it has been imported.

Raw handles must not be treated as globally transferable object identifiers.

### 14.2 Re-export lifetime

When process B receives an interface from A and wishes to provide it to C, B
normally exports its local proxy.

B therefore becomes an RPC intermediary:

```text
C -> B -> A
```

If B dies, C loses access through that export even if A remains alive.

This is intentional: the authority C received was exported by B.

This behaviour also avoids treating a task-specific handle originally created
for B as though it were a globally meaningful capability.

### 14.3 Locality independence

Consumers must use the defined Facet methods and property accessors regardless
of whether an interface is local or proxied.

Code whose behaviour changes merely because an implementation moves from the
same task to another task violates the Facet abstraction.

---

## 15. Startup ABI

The generic FacetOS startup ABI reserves a range of auxiliary-vector values
beginning at `AT_LOOS`.

The range:

```text
AT_LOOS ... AT_LOOS + 99
```

is reserved for `AT_FACET_*` definitions.

The primary generic bootstrap value is:

```text
AT_FACET_ROOT
```

whose payload is a bare `uint64_t` representing the initial/root Facet object
handle.

This is the handle from which the process obtains its
`IProcessEnvironment`.

### 15.1 Platform-specific auxv values

Platforms may require additional startup information.

Such entries belong to platform-specific portions of the reserved Facet range
and are named accordingly.

For seL4:

```text
AT_FACET_PLATFORM_SEL4_*
```

These may contain whatever additional values
`libfacet-platform-sel4` requires to establish the process's platform runtime.

Portable libc and application code must not interpret these entries.

Only the seL4 platform implementation may depend upon their meaning.

Conceptually:

```text
auxv
 |
 +-- AT_FACET_ROOT --------------------+
 |                                     |
 +-- AT_FACET_PLATFORM_SEL4_*          |
             |                         |
             v                         |
      platform initialization          |
             |                         |
             +-------------------------+
                       |
                       v
              IProcessEnvironment
```

The exact meaning of the bare `uint64_t` root handle on seL4 likewise belongs
entirely to `libfacet-platform-sel4`.

### 15.2 Linux startup

Linux development builds may use a Linux-specific bootstrap mechanism, such as
an environment variable containing an opaque locator understood by
`libfacet-platform-linux`.

This does not alter the FacetOS startup model.

Both mechanisms ultimately establish access to the process's root
`IProcessEnvironment`.

---

## 16. Intended build and source boundaries

The strongest implementation boundary in FacetOS is between portable
component code and platform-specific code.

For every component:

```text
src/<component>/
```

the portable implementation must contain no direct seL4 dependencies.

All seL4-specific implementation must reside beneath:

```text
src/<component>/platform/sel4/
```

This rule is intentionally strict.

The following appearing outside such a directory is considered a bug:

```text
#include <sel4/...>
seL4_*
sel4runtime-specific platform assumptions
raw seL4 capability manipulation
seL4 IPC
seL4 vSpace operations
seL4 CSpace operations
seL4-specific startup interpretation
```

The appropriate repair is not to add another conditional compilation block to
the portable source.

The repair is to identify the required abstraction and move the seL4 operation
behind the component's platform layer.

### 16.1 libfacet

The same principle produces:

```text
libfacet-common
        |
        +--------------------+
        |                    |
        v                    v
libfacet-platform-sel4   libfacet-platform-linux
```

`libfacet-common` contains no platform-specific implementation.

### 16.2 libc

Likewise, API personality and execution platform are orthogonal:

```text
                     API
              native      POSIX
                 |          |
                 +----+-----+
                      |
                   platform
                 /          \
              seL4          Linux
```

This produces build combinations such as:

```text
native + seL4
POSIX  + seL4
native + Linux
POSIX  + Linux
```

without introducing native/POSIX process types into FacetOS itself.

---

## 17. Architectural invariants

1. **There is no native/POSIX process personality.**

2. **There is no native/POSIX executable classification.**

3. **A POSIX program is distinguished solely by using `IPOSIXView`, normally
   indirectly through `libc-facet-posix`.**

4. **`IPOSIXView` is per-process and may also be present in the environment of
   native programs.**

5. **Native and POSIX programs may freely exist in the same domain and process
   ancestry.**

6. **`FacetShell` and other ordinary process creators use the same
   `IProcessManager` for native and POSIX executables.**

7. **Each domain receives a unique, restricted `IProcessManager`.**

8. **That `IProcessManager` can create processes only within the authority
   exported to its domain.**

9. **Parents explicitly construct the `IProcessEnvironment` passed to their
   children.**

10. **`dominit0 -> IDomainEnvironment -> dominit` and
    `parent -> IProcessEnvironment -> child` are analogous authority-delegation
    operations.**

11. **Any process may import and export Facet objects.**

12. **Exporting Facet objects is an ordinary part of using Facet APIs, not a
    privileged server-only operation.**

13. **Facet interface properties must always be accessed through their defined
    accessors, even for local objects.**

14. **Portable code must behave correctly if any local Facet interface is
    replaced by a proxy implementing the same interface.**

15. **`FacetHandle`s are task-specific and must not be treated as globally
    transferable identifiers.**

16. **When B wishes to make an interface received from A available to C, B
    normally exports its local proxy rather than forwarding A's raw handle.**

17. **Consequently, an interface re-exported by B normally becomes unavailable
    to C if B dies.**

18. **Domains are currently strictly isolated except for resources explicitly
    shared through system configuration.**

19. **General cross-domain process/interface export is intentionally not
    supported at present, although it may be added later.**

20. **Running a nested or modified `dominit` is not prohibited, but cannot
    manufacture additional authority.**

21. **A normal domain's `IPOSIXView` normally exposes `/posix` as its POSIX
    root.**

22. **A normal domain normally provides a synthetic read-only `/posix/etc`
    containing runtime-generated POSIX configuration such as `passwd` and
    `shadow`.**

23. **A pure POSIX domain instead gives its `IPOSIXView` the domain's real
    filesystem as `/` and therefore uses its real `/etc`.**

24. **A pure POSIX domain differs through initial environment construction,
    not through a special process personality.**

25. **`AT_FACET_*` occupies the reserved range from `AT_LOOS` through
    `AT_LOOS + 99`.**

26. **seL4-specific auxiliary-vector entries use
    `AT_FACET_PLATFORM_SEL4_*` names and are interpreted only by the seL4
    platform implementation.**

27. **Outside `src/<component>/platform/sel4/`, any direct reference or
    dependency upon seL4 is an architectural bug.**

28. **The correct response to such a dependency is to move the required
    functionality behind the platform abstraction, not to contaminate portable
    code with seL4 conditionals.**