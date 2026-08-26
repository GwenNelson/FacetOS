# FacetOS / libfacet / PolyFS Master TODO

## Phase 0 — Write `INTENDED_ARCHITECTURE.md`

Before asking Codex to change anything, write a complete description of FacetOS as Gwen actually intends it to work.

This is the architectural source of truth.

It should describe:

- seL4/kernel responsibilities;
- `dominit0`;
- `dominit`;
- domains and subdomains;
- process creation;
- authority delegation;
- Facet objects, exports, handles and proxies;
- capability transfer;
- filesystem/domain views;
- seats and terminals;
- native userland;
- POSIX userland;
- pure POSIX domains;
- drivers;
- object/process lifetime;
- startup ABI;
- intended build/library boundaries.

### Process startup ABI

Document the intended startup model:

```text
AT_FACET_ROOT = uint64_t
```

There should ideally be one Facet-specific auxv value.

On seL4:

```text
AT_FACET_ROOT
      ↓
libc generic platform startup
      ↓
libc-facet-platform-sel4
      ↓
libfacet-platform-sel4
      ↓
FacetHandle
      ↓
root proxy
      ↓
IProcessEnvironment
```

The interpretation of the bare `uint64_t` as seL4 capability/endpoint information belongs exclusively to the seL4 platform implementation.

Generic libc and userland must not understand seL4 capability representations.

### Intended layering

Document:

```text
application / portable library
             ↓
generated Facet interfaces
             ↓
       libfacet-common
             ↓
    Facet platform contract
             ↓
      platform backend
```

Initial platform backends:

- `libfacet-platform-sel4`
- `libfacet-platform-linux`

State the invariant:

> Source code chooses interfaces and personality. The build system chooses platform implementations.

---

## Phase 1 — Freeze the current implementation

Once `INTENDED_ARCHITECTURE.md` exists:

- [ ] Stop adding new architectural features.
- [ ] Do not begin the build-system refactor.
- [ ] Do not begin the Linux platform backend.
- [ ] Do not begin PolyFS.
- [ ] Do not ask Codex to make the existing implementation conform to the new architecture yet.
- [ ] Preserve the current implementation as something that can be inspected and understood.

The immediate goal is to establish:

> What does the code actually do right now?

---

## Phase 2 — Fresh-context forensic Codex audit

Start Codex in a fresh context.

Give it:

- `INTENDED_ARCHITECTURE.md`;
- `CURRENT_PLAN.md`;
- the current source tree;
- the current tests.

Tell it explicitly:

> Do not change anything yet.

Have Codex perform a complete audit of the existing implementation.

### 2.1 Current implementation inventory

Codex should describe:

- [ ] major components;
- [ ] libraries;
- [ ] executables;
- [ ] generated code;
- [ ] build structure;
- [ ] runtime relationships;
- [ ] current Facet object model;
- [ ] current export model;
- [ ] current proxy model;
- [ ] current IPC model;
- [ ] current process startup;
- [ ] current capability/handle plumbing;
- [ ] current libc structure;
- [ ] current domain/process structure.

This should describe **what exists**, not what Codex thinks ought to exist.

### 2.2 Explicit known-bug inventory

Codex must produce a finite list of every currently identifiable bug.

For each bug record:

- affected component;
- observed behaviour;
- intended behaviour;
- evidence;
- existing test coverage;
- whether an existing test should already catch it;
- smallest likely repair area.

Do not fix anything yet.

### 2.3 Explicit test-coverage inventory

Codex must identify:

- [ ] implemented features with no tests;
- [ ] partially tested features;
- [ ] important error paths without tests;
- [ ] ownership/lifetime behaviour without tests;
- [ ] RPC behaviour without tests;
- [ ] handle-transfer behaviour without tests;
- [ ] generated-code behaviour without tests;
- [ ] tests that can pass despite broken functionality;
- [ ] tests that merely reproduce current implementation rather than establish intended behaviour.

### 2.4 Explicit design-divergence inventory

Compare the current code against `INTENDED_ARCHITECTURE.md`.

Codex must explicitly list every place it believes the implementation diverges from Gwen's stated design intent.

Classify each as:

- definite divergence;
- likely divergence;
- ambiguous and requiring Gwen's judgement.

For every divergence describe:

```text
INTENDED:
...

CURRENT:
...

DIFFERENCE:
...
```

**Do not fix these divergences during the bug-fix phase unless they are independently genuine bugs.**

A working implementation that differs from the intended future architecture is not automatically a bug.

That distinction is crucial.

---

## Phase 3 — Fully document the current codebase before changing it

Now have Codex heavily document the implementation **exactly as it currently works**.

The purpose is to make the existing system understandable enough for Gwen to refactor deliberately.

Do not let Codex silently document the intended architecture as though it were already implemented.

### 3.1 Detailed source comments

Add detailed comments explaining:

- object ownership;
- object lifetime;
- handle ownership;
- handle cloning/release;
- buffer ownership;
- proxy construction;
- exported-object behaviour;
- dispatch;
- IPC;
- thread creation;
- capability handling;
- startup;
- generated-stub expectations;
- generated-dispatch expectations;
- domain/process relationships;
- libc/runtime relationships;
- unusual implementation decisions;
- temporary implementation decisions;
- assumptions and invariants.

Comments should explain **why and how**, not narrate C syntax.

Good:

```c
/*
 * The returned handle refers to the platform-specific exported representation
 * of this object. Generic libfacet code must treat the contents as opaque.
 *
 * In the current seL4 implementation, exporting also creates the servicing
 * thread responsible for dispatching incoming RPC requests.
 */
```

Bad:

```c
/* Set handle to NULL. */
handle = NULL;
```

### 3.2 Current-state documentation

Have Codex produce detailed documentation of:

> How FacetOS works today.

This documentation should explicitly distinguish itself from `INTENDED_ARCHITECTURE.md`.

For example:

```text
docs/CURRENT_IMPLEMENTATION.md
```

It should describe:

- current startup path;
- current object/export/proxy lifecycle;
- current IPC flow;
- current build structure;
- current libc arrangement;
- current seL4 dependencies;
- current tests;
- known limitations.

### 3.3 Preserve divergence markers

Where current implementation differs from intended architecture, documentation should say so explicitly rather than pretending the discrepancy doesn't exist.

For example:

```text
CURRENT IMPLEMENTATION:
...

INTENDED ARCHITECTURE:
...

STATUS:
Requires later architectural refactor.
```

At the end of this phase Gwen should be able to read the source and documentation and understand precisely what Codex has actually built.

---

## Phase 4 — Establish regression tests for known bugs

Before fixing each confirmed bug:

- [ ] Write or extend a test demonstrating it.
- [ ] Run the test.
- [ ] Confirm it fails.
- [ ] Confirm it fails for the expected reason.

Do not redesign anything.

The purpose is simply:

```text
bug exists
    ↓
test demonstrates bug
    ↓
minimal repair
    ↓
same test passes
```

For a bug where a useful automated regression test is genuinely impractical, document why.

---

## Phase 5 — Surgical known-bug repair

Now allow Codex to fix the confirmed bugs.

This is **not yet the architectural refactor**.

For each bug separately:

1. State the bug precisely.
2. Identify the relevant failing test.
3. Tell Codex to make the smallest possible implementation change.
4. Explicitly prohibit unrelated cleanup.
5. Explicitly prohibit refactoring.
6. Explicitly prohibit architectural redesign.
7. Inspect the diff.
8. Run the targeted test.
9. Confirm fail → pass.
10. Run the complete relevant test suite.
11. Update affected comments.
12. Update `CURRENT_IMPLEMENTATION.md`.
13. Commit separately.

The rule is:

> One bug, one surgical change, tests, documentation, commit.

Codex should be able to make these small, well-defined repairs without changing surrounding architecture.

---

## Phase 6 — Fill important missing test coverage

Once known bugs are fixed:

- [ ] Add tests for important currently-working features lacking coverage.
- [ ] Add ownership/lifetime tests.
- [ ] Add error-path tests.
- [ ] Add Facet handle tests.
- [ ] Add export/proxy tests.
- [ ] Add RPC tests.
- [ ] Add generated-code tests where appropriate.

These tests should describe the current intended behaviour without prematurely encoding planned architectural refactors.

Run the complete suite.

---

## Phase 7 — Produce the clean documented pre-refactor baseline

Before architectural cleanup begins, require:

- [ ] All known bugs fixed.
- [ ] All regression tests passing.
- [ ] Important existing functionality covered.
- [ ] Current code heavily commented.
- [ ] `CURRENT_IMPLEMENTATION.md` accurate.
- [ ] `INTENDED_ARCHITECTURE.md` accurate.
- [ ] Known differences between the two explicitly listed.
- [ ] No known undocumented implementation behaviour.
- [ ] Clean commit.

Create a baseline tag/commit.

This becomes:

> The known-working, understood, pre-refactor FacetOS implementation.

---

## Phase 8 — Manually review the divergence list

Now Gwen reviews every difference between:

```text
INTENDED_ARCHITECTURE.md
           ↕
CURRENT_IMPLEMENTATION.md
           ↕
current source
```

For every divergence decide:

- implementation is wrong and should change;
- architecture document needs correction;
- implementation is acceptable;
- issue should be deferred.

Do not let Codex make these architectural decisions autonomously.

---

## Phase 9 — Begin architectural conformance refactoring

Only now start deliberately making the code match the intended architecture.

Use the same surgical discipline.

For each architectural change:

1. Describe exactly one desired change.
2. Identify the invariant being established.
3. Update/add tests where appropriate.
4. Make the smallest coherent implementation change.
5. Run tests.
6. Inspect diff.
7. Update comments.
8. Update `CURRENT_IMPLEMENTATION.md`.
9. Verify it now agrees with `INTENDED_ARCHITECTURE.md`.
10. Commit.

Avoid giant "refactor architecture" prompts.

---

## Phase 10 — Build-system isolation refactor

Now isolate seL4-specific implementation details.

### 10.1 Facet component libraries

Maintain:

```text
libfacet-common.a
libfacet-common.so

libfacet-platform-sel4.a

libfacet-platform-linux.a
libfacet-platform-linux.so
```

`libfacet-common` must remain platform neutral.

### 10.2 Aggregate seL4 Facet library

Merge:

```text
libfacet-common.a
libfacet-platform-sel4.a
required seL4 libraries
required seL4 runtime
        ↓
libfacet-sel4.a
```

Programs targeting FacetOS should not need to know which seL4 implementation libraries Facet requires.

### 10.3 Component versus deployment libraries

Keep modular component libraries available internally.

Use aggregate deployment libraries at application/sysroot boundaries.

Do not destroy modularity merely to simplify final application linkage.

---

## Phase 11 — libc component structure

Maintain personality libraries:

```text
libc-facet-native.a
libc-facet-native.so

libc-facet-posix.a
libc-facet-posix.so
```

Maintain platform glue:

```text
libc-facet-platform-sel4.a
libc-facet-platform-sel4.so

libc-facet-platform-linux.a
libc-facet-platform-linux.so
```

### 11.1 seL4 deployment archives

Produce:

```text
libc-facet-native-sel4.a
libc-facet-posix-sel4.a
```

Each should merge all required static dependencies.

For native:

```text
libc-facet-native.a
libc-facet-platform-sel4.a
libfacet-sel4.a
required runtime support
        ↓
libc-facet-native-sel4.a
```

For POSIX:

```text
libc-facet-posix.a
libc-facet-platform-sel4.a
libfacet-sel4.a
required runtime support
        ↓
libc-facet-posix-sel4.a
```

Applications should be able to treat the appropriate archive as simply `libc.a`.

### 11.2 Shared composition libraries

Produce:

```text
libc-facet-native-sel4.so
libc-facet-posix-sel4.so

libc-facet-native-linux.so
libc-facet-posix-linux.so
```

These should preferably be thin dependency/composition libraries rather than duplicated implementations.

For example:

```text
libc-facet-native-linux.so
    → libc-facet-native.so
    → libc-facet-platform-linux.so
    → libfacet-platform-linux.so
    → libfacet-common.so
```

and:

```text
libc-facet-posix-linux.so
    → libc-facet-posix.so
    → libc-facet-platform-linux.so
    → libfacet-platform-linux.so
    → libfacet-common.so
```

The top-level shared library may contain almost no implementation code of its own.

---

## Phase 12 — Four sysroots

Construct:

```text
sysroots/
├── native-sel4/
├── posix-sel4/
├── native-linux/
└── posix-linux/
```

Each should look conventional to the compiler:

```text
sysroot/
└── usr/
    ├── include/
    └── lib/
        ├── libc.a
        └── libc.so
```

### 12.1 Headers

Maintain a canonical header staging area.

Prefer symlinks rather than four copies of identical headers.

For example:

```text
build/sysroot-common/usr/include/
```

Platform-specific public headers should be minimized.

Ordinary application source should not include seL4 headers simply because its target platform happens to be seL4.

### 12.2 libc selection

#### `native-sel4`

```text
libc.a → libc-facet-native-sel4.a
libc.so → libc-facet-native-sel4.so
```

#### `posix-sel4`

```text
libc.a → libc-facet-posix-sel4.a
libc.so → libc-facet-posix-sel4.so
```

#### `native-linux`

```text
libc.a / libc.so → native Linux Facet deployment library
```

#### `posix-linux`

```text
libc.a / libc.so → IPOSIXView-backed Linux deployment library
```

The desired application build becomes conceptually:

```sh
$CC --sysroot=<selected-sysroot> program.c
```

rather than application build files knowing the underlying implementation libraries.

---

## Phase 13 — Make platform choice principally a link/build choice

Architectural target:

> As much of FacetOS userland as possible should build for another supported execution platform simply by selecting another sysroot/library set.

Audit for:

- [ ] unnecessary `#ifdef` platform selection;
- [ ] direct seL4 includes in portable code;
- [ ] Linux includes above Linux platform components;
- [ ] application knowledge of platform handles;
- [ ] applications explicitly linking implementation libraries.

Treat unnecessary occurrences as architectural smells.

---

## Phase 14 — Implement `libfacet-platform-linux`

Implement the existing Facet platform contract on Linux.

Support:

- [ ] object export;
- [ ] synchronous RPC;
- [ ] transferable exported-object representations;
- [ ] importing those representations into `FacetHandle`;
- [ ] normal `libfacet_proxy_from()`;
- [ ] handle attachments;
- [ ] returned interfaces;
- [ ] cloning/release;
- [ ] multiple processes.

The transport remains private to the backend.

Investigate:

- SysV message queues;
- Unix-domain sockets;
- other suitable Linux IPC if justified.

Select whichever best implements the intended Facet semantics.

Do not alter generic Facet abstractions merely to suit Linux.

---

## Phase 15 — Linux startup

FacetOS/seL4:

```text
AT_FACET_ROOT = uint64_t
       ↓
libc platform setup
       ↓
libfacet-platform-sel4
       ↓
FacetHandle
       ↓
IProcessEnvironment
```

Linux:

```text
FACET_ROOT=<opaque platform locator>
       ↓
libc-facet-platform-linux
       ↓
libfacet-platform-linux
       ↓
FacetHandle
       ↓
IProcessEnvironment
```

The Linux root locator comes from the environment, not an inherited fd.

`FACET_ROOT` must be opaque outside `libfacet-platform-linux`.

It may internally identify:

- Unix-domain-socket information;
- SysV message queue information;
- runtime identity;
- exported-object identity;
- some other future Linux representation.

Generic libc must not care.

---

## Phase 16 — Linux privileged-operation test library

Create one reusable:

```text
tests/linux/libfacet-test-preload.so
```

Use it through:

```text
LD_PRELOAD=.../libfacet-test-preload.so
```

Do not create separate preload libraries for individual programs unless genuinely unavoidable.

### 16.1 Initially intercepted operations

Support as needed:

- `setuid`
- `seteuid`
- `setreuid`
- `setresuid`
- `setgid`
- `setegid`
- `setregid`
- `setresgid`
- `setgroups`
- `initgroups`
- `chroot`

Add other privileged calls only when real userland requires them.

### 16.2 Configurable return values

Each intercepted call should obey environment variables defining its result.

Examples:

```text
SETUID_RETVAL=0
SETGID_RETVAL=0
INITGROUPS_RETVAL=0
CHROOT_RETVAL=0
```

Failure testing:

```text
SETUID_RETVAL=-1
SETUID_ERRNO=EPERM
```

Use one consistent naming scheme.

Tests should be able to independently configure each operation.

### 16.3 Privileged-call logging

The library should optionally send records of intercepted calls to a destination specified through environment variables.

For example:

```text
FACET_TEST_PRIVCALL_LOG=<destination>
```

The eventual destination mechanism can be:

- Unix socket;
- FIFO;
- file;
- another suitable deterministic test transport.

Each record should contain at least:

```text
sequence
PID
function
arguments
configured return value
configured errno
```

Tests should therefore be able to assert sequences such as:

```text
setgid(1000)
initgroups("gwen", 1000)
setuid(1000)
```

rather than merely checking that the application exited successfully.

### 16.4 Fake `chroot`

The same preload library should support fake chroot when explicitly requested.

Conceptually:

```text
FACET_TEST_FAKE_CHROOT=/tmp/facet-test/root
CHROOT_RETVAL=0
```

It should:

- record the requested `chroot()` call;
- obey configured return/errno behaviour;
- maintain the configured fake-root state;
- redirect relevant subsequent pathname operations through the fake root where required;
- remain deterministic.

This is application testing, **not security isolation**.

### 16.5 Keep test interposition out of runtime libraries

Do not put privileged-call faking into:

```text
libfacet-platform-linux
libc-facet-platform-linux
```

Those implement actual Linux execution support.

`libfacet-test-preload.so` is test instrumentation only.

---

## Phase 17 — Three Linux program-testing modes

These are deliberately separate because they test different things.

### 17.1 Native Facet program on Linux

Compile using:

```text
native-linux sysroot
```

Execution path:

```text
native Facet program
        ↓
libc-facet-native-linux
        ↓
Facet APIs
        ↓
libfacet-platform-linux
```

This tests:

- native Facet application semantics;
- Facet startup;
- `IProcessEnvironment`;
- Facet RPC;
- authority/interface use;
- Linux backend behaviour.

The program must not silently become an ordinary glibc program.

### 17.2 POSIX program through IPOSIXView on Linux

Compile using:

```text
posix-linux sysroot
```

Execution path:

```text
POSIX application
       ↓
minimal libc-facet-posix
       ↓
IPOSIXView
       ↓
Facet
       ↓
libfacet-platform-linux
```

This specifically tests **FacetOS's POSIX implementation**.

#### Critical libc constraint

The Linux-backed IPOSIXView libc must **not pull in full glibc**.

Otherwise tests can accidentally exercise glibc rather than `IPOSIXView`.

The minimal libc should route POSIX functionality through the known Facet POSIX interfaces.

Where its Linux runtime implementation unavoidably needs host kernel functionality merely to execute as a Linux process, it should invoke only a deliberately small and explicit set of known Linux syscalls.

Maintain an audited list of those syscalls.

For every host Linux syscall used, document:

- why it is required;
- which runtime component uses it;
- why it cannot reasonably go through `IPOSIXView`;
- whether application-visible POSIX semantics can accidentally bypass FacTATION.md".
9. Verify it now agrees with "INTENDED_ARCHITECTURE.md".
10. Commit.

Avoid giant "refactor architecture" prompts.

---

Phase 10 — Build-system isolation refactor

Now isolate seL4-specific implementation details.

Maintain component libraries:

libfacet-common.a
libfacet-common.so

libfacet-platform-sel4.a
libfacet-platform-linux.a
libfacet-platform-linux.so

Produce an aggregate static seL4 deployment library:

libfacet-common.a
libfacet-platform-sel4.a
required seL4 libraries
required seL4 runtime
        ↓
libfacet-sel4.a

Programs should not need to know which seL4 libraries Facet requires.

---

Phase 11 — libc component structure

Maintain personality libraries:

libc-facet-native.a
libc-facet-native.so

libc-facet-posix.a
libc-facet-posix.so

Maintain platform glue:

libc-facet-platform-sel4.a
libc-facet-platform-sel4.so

libc-facet-platform-linux.a
libc-facet-platform-linux.so

seL4 deployment archives

Produce:

libc-facet-native-sel4.a
libc-facet-posix-sel4.a

Each should merge all required static dependencies so applications can treat the appropriate archive as "libc.a".

Shared composition libraries

Produce:

libc-facet-native-sel4.so
libc-facet-posix-sel4.so

libc-facet-native-linux.so
libc-facet-posix-linux.so

These should preferably be thin dependency/composition libraries rather than duplicated implementations.

---

Phase 12 — Four sysroots

Construct:

sysroots/
├── native-sel4/
├── posix-sel4/
├── native-linux/
└── posix-linux/

Use common/symlinked headers wherever possible.

Each sysroot should present conventional:

usr/include/
usr/lib/libc.a
usr/lib/libc.so

with "libc" selecting the appropriate personality/platform implementation.

Applications should principally change platform by changing sysroot.

---

Phase 13 — Implement "libfacet-platform-linux"

Implement the existing Facet platform contract on Linux.

Support:

- object export;
- synchronous RPC;
- transferable exported-object representations;
- import to "FacetHandle";
- normal "libfacet_proxy_from()";
- handle attachments;
- returned interfaces;
- cloning/release;
- multiple processes.

Transport remains private to the backend.

Investigate SysV msgq and Unix-domain sockets and select whichever best implements the desired Facet semantics.

---

Phase 14 — Linux startup

FacetOS/seL4:

AT_FACET_ROOT = uint64_t
       ↓
platform setup
       ↓
IProcessEnvironment

Linux:

FACET_ROOT=<opaque platform locator>
       ↓
libc-facet-platform-linux
       ↓
libfacet-platform-linux
       ↓
IProcessEnvironment

Do not expose the interpretation of "FACET_ROOT" outside the Linux platform backend.

---

Phase 15 — Linux privileged-operation test library

Create one reusable:

tests/linux/libfacet-test-preload.so

Use "LD_PRELOAD" for test-only interception of privileged operations.

Support at least:

- "setuid"
- "seteuid"
- "setreuid"
- "setresuid"
- "setgid"
- "setegid"
- "setregid"
- "setresgid"
- "setgroups"
- "initgroups"
- "chroot"

Configure behaviour through environment variables such as:

SETUID_RETVAL=0
SETGID_RETVAL=0
CHROOT_RETVAL=0

SETUID_RETVAL=-1
SETUID_ERRNO=EPERM

Provide a test-configured logging destination.

Log:

- function;
- arguments;
- process;
- sequence;
- configured result;
- errno.

Support explicitly enabled fake-chroot behaviour.

Keep all this out of production Linux Facet libraries.

---

Phase 16 — Three Linux program-testing modes

A. Native Facet-on-Linux

Compile using the "native-linux" sysroot.

Tests:

native program
      ↓
libc-facet-native-linux
      ↓
Facet
      ↓
libfacet-platform-linux

B. Facet POSIX-on-Linux

Compile using "posix-linux".

Tests:

POSIX program
      ↓
minimal libc-facet-posix
      ↓
IPOSIXView
      ↓
libfacet-platform-linux

The Linux-backed IPOSIXView libc must not pull in full glibc.

Its unavoidable Linux implementation support should use only a small, explicit, audited set of known Linux syscalls.

The POSIX APIs being tested must actually go through "IPOSIXView".

C. Ordinary host POSIX

Separately compile the same POSIX source using ordinary host GCC and host libc:

source
  ↓
host gcc
  ↓
host libc

This establishes that supposedly POSIX applications really are conventional POSIX source.

Use "libfacet-test-preload.so" where privileged operations must be simulated.

Thus:

same POSIX source
       /       \
      /         \
host libc      IPOSIXView
    │               │
tests source     tests Facet's
portability      POSIX implementation

---

Phase 17 — Linux program-test framework

Create reusable test infrastructure for:

- fake Facet objects;
- exported interfaces;
- root "IProcessEnvironment";
- "FACET_ROOT";
- PTYs;
- process spawning;
- stdin/stdout/stderr;
- privileged-call logs;
- configured preload results;
- RPC expectations;
- handle leak/lifetime checks.

Individual tests should then be small:

FacetLoginTester
LoginTester
DominitTester
...

---

Phase 18 — Linux "dominit"

Eventually run real "dominit" linked against the Linux platform.

Give it spoofed initial authority and allow it to launch Linux processes representing a Facet domain.

Use fake seats backed by PTYs and/or sockets.

Target:

DominitTester
      ↓
    dominit
      ├── native FacetLogin
      ├── IPOSIXView /bin/login
      └── POSIX domain processes

Keep Linux process creation beneath the relevant Facet/platform interfaces rather than teaching "dominit" Linux-specific semantics.

---

Phase 19 — IDL persistence/version support

Before PolyFS durable typed properties:

- support historical auto-UUID IDL definitions;
- version generated definitions/files;
- retain legacy decoding stubs;
- ensure schema changes produce appropriate new UUIDs;
- test old serialized blobs against historical definitions.

---

Phase 20 — PolyFS

Fixed model:

Every file:
    binary blob
    optional MIME type
    typed property sets by UUID
    metadata as section,key,value

Typed property blobs are serialized by IDL-generated code in defined field order.

Old UUIDs retain old schema meanings.

Unknown property blobs must be preservable.

---

Phase 21 — "libpolyfs"

Hard boundary:

IBlockDevice
     ↓
 libpolyfs
     ↓
 IFileStore

"libpolyfs" speaks only Facet interfaces.

No:

- Linux;
- seL4;
- FUSE;
- sockets;
- msgq;
- host filesystem APIs.

Goal: where ABI permits, literally the same "libpolyfs.a" is usable on Linux and inside FacetOS.

---

Phase 22 — Linux PolyFS environment

Implement:

host file
   ↓
Linux file-backed IBlockDevice
   ↓
libpolyfs
   ↓
IFileStore

Then build:

- mtools-style native PolyFS tools;
- FUSE adapter;
- xattr access to MIME/metadata/typed properties;
- standalone Linux PolyFS server;
- remote "IFileStore" access through "libfacet-platform-linux".

---

Phase 23 — Cross-platform test expansion

Run as much of the test suite and userland as possible through:

1. direct/local Linux Facet objects;
2. Linux interprocess Facet RPC;
3. seL4 Facet RPC;
4. IPOSIXView-on-Linux;
5. ordinary host POSIX where applicable.

By the time QEMU/FacetOS integration testing begins, individual program logic should already have been extensively exercised outside FacetOS.

---

Core working rule

At every stage:

«Understand first. Test bugs second. Repair bugs surgically third. Only then refactor architecture.»

And during architectural work:

«One intentional architectural change at a time, followed immediately by tests, comments, documentation and review.»

Do not allow Codex to combine bug fixing, cleanup, documentation and architectural redesign into one change.
