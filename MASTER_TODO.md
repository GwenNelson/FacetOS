FacetOS / libfacet / PolyFS Master TODO

Phase 0 — Write "INTENDED_ARCHITECTURE.md"

Before asking Codex to change anything, write a complete description of FacetOS as Gwen actually intends it to work.

This is the architectural source of truth.

It should describe:

- seL4/kernel responsibilities;
- "dominit0";
- "dominit";
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

Process startup ABI

Document the intended startup model:

AT_FACET_ROOT = uint64_t

There should ideally be one Facet-specific auxv value.

On seL4:

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

The interpretation of the bare "uint64_t" as seL4 capability/endpoint information belongs exclusively to the seL4 platform implementation.

Generic libc and userland must not understand seL4 capability representations.

Intended layering

Document:

application / portable library
             ↓
generated Facet interfaces
             ↓
       libfacet-common
             ↓
    Facet platform contract
             ↓
      platform backend

Initial platform backends:

- "libfacet-platform-sel4"
- "libfacet-platform-linux"

State the invariant:

«Source code chooses interfaces and personality. The build system chooses platform implementations.»

---

Phase 1 — Freeze the current implementation

Once "INTENDED_ARCHITECTURE.md" exists:

- [ ] Stop adding new architectural features.
- [ ] Do not begin the build-system refactor.
- [ ] Do not begin the Linux platform backend.
- [ ] Do not begin PolyFS.
- [ ] Do not ask Codex to make the existing implementation conform to the new architecture yet.
- [ ] Preserve the current implementation as something that can be inspected and understood.

The immediate goal is to establish:

«What does the code actually do right now?»

---

Phase 2 — Fresh-context forensic Codex audit

Start Codex in a fresh context.

Give it:

- "INTENDED_ARCHITECTURE.md";
- "CURRENT_PLAN.md";
- the current source tree;
- the current tests.

Tell it explicitly:

«Do not change anything yet.»

Have Codex perform a complete audit of the existing implementation.

2.1 Current implementation inventory

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

This should describe what exists, not what Codex thinks ought to exist.

2.2 Explicit known-bug inventory

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

2.3 Explicit test-coverage inventory

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

2.4 Explicit design-divergence inventory

Compare the current code against "INTENDED_ARCHITECTURE.md".

Codex must explicitly list every place it believes the implementation diverges from Gwen's stated design intent.

Classify each as:

- definite divergence;
- likely divergence;
- ambiguous and requiring Gwen's judgement.

For every divergence describe:

INTENDED:
...

CURRENT:
...

DIFFERENCE:
...

Do not fix these divergences during the bug-fix phase unless they are independently genuine bugs.

A working implementation that differs from the intended future architecture is not automatically a bug.

That distinction is crucial.

---

Phase 3 — Fully document the current codebase before changing it

Now have Codex heavily document the implementation exactly as it currently works.

The purpose is to make the existing system understandable enough for Gwen to refactor deliberately.

Do not let Codex silently document the intended architecture as though it were already implemented.

3.1 Detailed source comments

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

Comments should explain why and how, not narrate C syntax.

Good:

/*
 * The returned handle refers to the platform-specific exported representation
 * of this object. Generic libfacet code must treat the contents as opaque.
 *
 * In the current seL4 implementation, exporting also creates the servicing
 * thread responsible for dispatching incoming RPC requests.
 */

Bad:

/* Set handle to NULL. */
handle = NULL;

3.2 Current-state documentation

Have Codex produce detailed documentation of:

«How FacetOS works today.»

This documentation should explicitly distinguish itself from "INTENDED_ARCHITECTURE.md".

For example:

docs/CURRENT_IMPLEMENTATION.md

It should describe:

- current startup path;
- current object/export/proxy lifecycle;
- current IPC flow;
- current build structure;
- current libc arrangement;
- current seL4 dependencies;
- current tests;
- known limitations.

3.3 Preserve divergence markers

Where current implementation differs from intended architecture, documentation should say so explicitly rather than pretending the discrepancy doesn't exist.

For example:

CURRENT IMPLEMENTATION:
...

INTENDED ARCHITECTURE:
...

STATUS:
Requires later architectural refactor.

At the end of this phase Gwen should be able to read the source and documentation and understand precisely what Codex has actually built.

---

Phase 4 — Establish regression tests for known bugs

Before fixing each confirmed bug:

- [ ] write or extend a test demonstrating it;
- [ ] run the test;
- [ ] confirm it fails;
- [ ] confirm it fails for the expected reason.

Do not redesign anything.

The purpose is simply:

bug exists
    ↓
test demonstrates bug
    ↓
minimal repair
    ↓
same test passes

For a bug where a useful automated regression test is genuinely impractical, document why.

---

Phase 5 — Surgical known-bug repair

Now allow Codex to fix the confirmed bugs.

This is not yet the architectural refactor.

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
12. Update "CURRENT_IMPLEMENTATION.md".
13. Commit separately.

The rule is:

«One bug, one surgical change, tests, documentation, commit.»

Codex should be able to make these small, well-defined repairs without changing surrounding architecture.

---

Phase 6 — Fill important missing test coverage

Once known bugs are fixed:

- [ ] add tests for important currently-working features lacking coverage;
- [ ] add ownership/lifetime tests;
- [ ] add error-path tests;
- [ ] add Facet handle tests;
- [ ] add export/proxy tests;
- [ ] add RPC tests;
- [ ] add generated-code tests where appropriate.

These tests should describe the current intended behaviour without prematurely encoding planned architectural refactors.

Run the complete suite.

---

Phase 7 — Produce the clean documented pre-refactor baseline

Before architectural cleanup begins, require:

- [ ] all known bugs fixed;
- [ ] all regression tests passing;
- [ ] important existing functionality covered;
- [ ] current code heavily commented;
- [ ] "CURRENT_IMPLEMENTATION.md" accurate;
- [ ] "INTENDED_ARCHITECTURE.md" accurate;
- [ ] known differences between the two explicitly listed;
- [ ] no known undocumented implementation behaviour;
- [ ] clean commit.

Create a baseline tag/commit.

This becomes:

«The known-working, understood, pre-refactor FacetOS implementation.»

---

Phase 8 — Manually review the divergence list

Now Gwen reviews every difference between:

INTENDED_ARCHITECTURE.md
           ↕
CURRENT_IMPLEMENTATION.md
           ↕
current source

For every divergence decide:

- implementation is wrong and should change;
- architecture document needs correction;
- implementation is acceptable;
- issue should be deferred.

Do not let Codex make these architectural decisions autonomously.

---

Phase 9 — Begin architectural conformance refactoring

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
8. Update "CURRENT_IMPLEMENTATION.md".
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
