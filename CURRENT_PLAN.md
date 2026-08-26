# Native/POSIX Namespace Completion and Verification

## Summary

Commit the existing uncommitted POSIX-domain work as one baseline commit, then replace the current partial `/etc` interception with a proper per-domain namespace and personality-view model.

Namespace and personality policy belongs to each domain's `dominit`, not to `dominit0`.

`dominit0` provides the privileged mechanisms and foundational capabilities required to construct and run domains. Each `dominit` uses the capabilities delegated to its domain, together with that domain's configuration, to construct the filesystem namespaces, personality views, services, and process environments that the domain requires.

**The current distinction between domain 0 and domain 1 is configuration, not architecture.**

The checked-in configuration currently specifies:

* domain 0 as a native FacetOS domain that also exposes a POSIX view to selected POSIX processes;
* domain 1 as a pure POSIX domain.

Nothing in the implementation should inherently associate domain 0 with native FacetOS semantics or domain 1 with POSIX semantics.

The same mechanisms must permit a future configuration in which:

* domain 1 or any other domain is native-only;
* domain 1 or any other domain is native with one or more POSIX views;
* multiple domains independently provide native+POSIX environments;
* a domain is pure POSIX;
* a domain exposes some other future personality;
* a domain hosts a VM or other execution environment;
* or a domain exposes any combination of views and services that its configuration and delegated capabilities permit.

For the **current configuration**, domain 0 has two distinct filesystem views:

* its native FacetOS namespace, exposing `/FacetOS`, `/Data`, `/home`, `README`, and the physical `/posix` backing tree;
* a domain-local POSIX namespace exported through `IPOSIXView`, whose `/` maps to native `/posix` and whose `/etc` is a synthetic mounted filesystem that never physically exists in `system.initrd`.

For the **current configuration**, domain 1 is a pure POSIX domain whose `IPOSIXView` maps directly onto its independent initrd root, including its real `/etc`.

These are configured instances of generic domain mechanisms, not special domain-number behavior.

## Authority and ownership model

* `dominit0` owns only machine-wide/root-task mechanisms and explicitly delegates capabilities to each domain's `dominit`.
* `dominit0` must not implement domain-specific POSIX path translation, synthetic `/etc` policy, user-visible namespace construction, executable namespace policy, or behavior conditional on a particular domain number.
* Each `dominit` owns the services and policy specific to its domain, according to its configuration and delegated capabilities.
* A `dominit` may construct a native filesystem view, an `IPOSIXView`, both, or neither.
* The ability to construct an `IPOSIXView` must not depend on being domain 0.
* The ability to host native FacetOS applications must not depend on being domain 0.
* Pure-POSIX behavior must not depend on being domain 1.
* No domain ID may implicitly determine a personality or namespace type.
* Where process creation requires authority that must remain in `dominit0`, expose only a narrow private launch mechanism to the appropriate `dominit`; do not expose that mechanism to ordinary processes.
* `dominit` decides which interfaces and capabilities are placed into each process's `IProcessEnvironment`.
* Native processes receive only the explicitly selected native interfaces.
* POSIX processes receive `IPOSIXView` and the restricted POSIX process/runtime interfaces required by `libc-posix`; they must not implicitly receive native filesystem, authentication, session-management, security, or unrestricted process-manager capabilities.
* Future hybrid applications may explicitly receive both native interfaces and `IPOSIXView`, but hybrid support is outside this tranche.

A useful conceptual boundary is:

```text
dominit0
    │
    │ privileged mechanisms and delegated authority
    ▼
dominit for domain N
    │
    ├── configured native view, if requested
    ├── configured POSIX view(s), if requested
    ├── other configured services/views
    └── configured process environments
             │
             ▼
        domain processes
```

Domain `N` is intentionally arbitrary.

## Current system-initrd layout

For the currently configured domain 0, change its system initrd layout as follows:

* Move POSIX applications to physical:

  * `/posix/bin/login`
  * `/posix/bin/sh`
  * `/posix/bin/ls`
  * `/posix/bin/cat`
* Do not package physical `/bin`, `/etc`, or `/posix/etc` into `system.initrd`.
* Keep native applications under `/FacetOS`.
* Keep native data under the native namespace, including `/Data` and `/home`.
* Preserve `/posix` as a visible native backing tree so native FacetOS applications may inspect it directly.

The resulting current domain-0 native namespace should resemble:

```text
/
├── FacetOS/
├── Data/
├── home/
├── posix/
│   └── bin/
│       ├── login
│       ├── sh
│       ├── ls
│       └── cat
└── README
```

There must be no physical domain-0 `/etc` and no physical `/posix/etc`.

For the currently configured pure-POSIX domain 1:

* preserve its independently packaged real `/bin`, `/sbin`, `/etc`, `/home`, and other POSIX content in `child.initrd`;
* do not require a native backing namespace merely because domain 0 currently has one.

These layouts are consequences of the current domain configurations. They must not become hard-coded assumptions about domain numbers.

## Generic domain-local POSIX namespace service

Implement POSIX namespace construction as a domain-local facility owned by `dominit`.

A domain configured with a POSIX view must be able to construct that view independently of its numeric domain ID.

For the **currently configured native+POSIX domain 0**:

* POSIX `/` maps to the native `/posix` backing tree.
* `/bin/sh` therefore resolves to native `/posix/bin/sh`.
* `/bin/login`, `/bin/ls`, `/bin/cat`, and other POSIX executables resolve through the same namespace mechanism used for ordinary POSIX file operations.
* The `/posix` backing prefix must never be visible through that `IPOSIXView`.
* POSIX path handling must support:

  * absolute paths;
  * relative paths;
  * per-process CWD;
  * `.`;
  * `..`;
  * directory enumeration;
  * normal missing-path and wrong-type errors.
* POSIX `/etc` is an explicit synthetic mount implemented by this configured POSIX namespace.
* Synthetic `/etc` exposes:

  * `/etc/passwd`;
  * `/etc/shadow`;
  * `/etc/fstab`.
* These files are generated from configured domain/user data and are read-only.
* `/etc/shadow` is readable only under the appropriate root credentials.
* Unsupported writes return normal POSIX errors.
* Synthetic `/etc` participates normally in open/stat-equivalent operations, directory listing, `chdir`, relative lookup, and CWD handling.
* No synthetic `/etc` content may be physically stored in `system.initrd`.

Do not automatically expose native `/FacetOS` or `/Data` through a POSIX view merely because those paths exist in the domain's native namespace.

If native trees are to appear inside a POSIX namespace, they should eventually be explicit configured mounts.

For the **currently configured pure-POSIX domain 1**:

* its `dominit` constructs its own `IPOSIXView`;
* POSIX `/` maps directly to that domain's own initrd root;
* `/etc` is the real `/etc` contained in `child.initrd`;
* it must not depend on domain 0's synthetic `/etc`, backing tree, or namespace state.

The implementation should model these as at least two configurations of the same namespace/view machinery:

```text
Native + POSIX configuration:

native namespace
    /
    └── posix/       ← backing tree

IPOSIXView
    /
    ├── bin/         ← backed by native /posix/bin
    └── etc/         ← synthetic mount


Pure POSIX configuration:

IPOSIXView
    /
    ├── bin/
    ├── sbin/
    └── etc/         ← real files in this domain's initrd
```

A future domain must be able to choose either arrangement regardless of its domain ID.

## Process ABI and executable selection

Do not infer process ABI from domain number.

Do not infer process ABI solely from a raw pathname prefix supplied by the caller.

Executable lookup must occur through the caller's configured namespace/profile.

After resolution:

* executables resolved through a native FacetOS namespace may run with the native FacetOS process profile;
* executables resolved through an `IPOSIXView` run with the POSIX process profile;
* a pure-POSIX domain uses the same POSIX profile mechanism as a POSIX view belonging to a native domain.

For the current configuration this means:

* domain 0 `/FacetOS/*` → native profile;
* domain 0 POSIX `/bin/*` → POSIX profile;
* domain 1 `/bin/*` and `/sbin/*` → POSIX profile.

These mappings must arise from configuration and namespace resolution, **not from tests such as `domain_id == 0` or `domain_id == 1`.**

What makes a program POSIX is its runtime contract and process environment, not an unusual launch operation.

`/bin/login` remains an ordinary executable.

A POSIX process linked against `libc-posix` observes the system through `IPOSIXView` and the other restricted POSIX-facing interfaces supplied to it.

A native process linked against the FacetOS runtime observes the native interfaces supplied through `IProcessEnvironment`.

`dominit` constructs the appropriate `IProcessEnvironment`; user processes never directly invoke the privileged `dominit0` launch mechanism.

## POSIX process and session behavior

Complete generic POSIX session behavior as follows:

* Export `FACET_TERMINAL` using the configured terminal reference.
* POSIX login banners show the actual domain ID and configured terminal.
* Native `FacetLogin` banners likewise show their actual configured domain and terminal rather than assuming domain 0.
* Honor each user's configured `posix_shell`.
* Foreground POSIX spawn preserves:

  * domain;
  * POSIX profile;
  * authenticated principal/session;
  * terminal;
  * CWD;
  * process environment;
  * PID semantics;
  * parent/wait relationship.
* Keep `facet_spawn()` as the current explicit spawn bridge.
* `facet_spawn()` is implemented by a domain-local service owned by the relevant `dominit`.
* That service may call a narrow private launch facility in `dominit0` where seL4/root-task authority requires it.
* POSIX applications never receive the underlying native process-manager or root launch capability directly.

None of these mechanisms may assume that POSIX means domain 1 or native+POSIX means domain 0.

## POSIX applications

Finish `/bin/sh`:

* support whitespace tokenization;
* support quoted arguments;
* support backslash escaping;
* explicitly reject unsupported pipes, redirects, globbing, background jobs, and shell expansion syntax;
* show the current POSIX CWD before the prompt;
* use `#` when authenticated as root;
* use `$` otherwise;
* execute `ls`, `cat`, and slash-containing paths as foreground POSIX child processes;
* propagate useful command failure status and diagnostics.

Finish `/bin/ls`:

* accept zero or more operands;
* list the CWD when no operand is supplied;
* support files and directories appropriately;
* emit a diagnostic and nonzero exit status for missing paths;
* emit an appropriate diagnostic and nonzero exit status for wrong-type operands.

Finish `/bin/cat`:

* accept one or more file operands;
* emit diagnostics for missing or unreadable files;
* return nonzero status if any requested operation fails.

Native `FacetShell` must likewise report command failures consistently for paths visible through whatever native namespace has been delegated to it.

## Current domain configuration

The checked-in configuration currently assigns:

```text
seat0.ttyS0 → domain 0 → native FacetLogin
seat1.tty1  → domain 0 → native FacetLogin
seat1.tty2  → domain 0 → POSIX /bin/login
seat1.tty3  → domain 1 → pure-POSIX /sbin/init → /bin/login
```

This section describes **the current test/demo configuration only**.

No implementation may rely on these domain IDs or terminal assignments.

It must remain valid to later configure, for example:

```text
domain 0 → native only
domain 1 → native + POSIX view
domain 2 → native + POSIX view
domain 3 → pure POSIX
domain 4 → some future personality
domain 5 → VM-hosting domain
```

without adding domain-number-specific code.

`tty2` does not use a special POSIX executable-launch shortcut. `/bin/login` is an ordinary executable whose path resolves through the configured POSIX namespace and whose process receives the configured POSIX environment.

For the current pure-POSIX domain 1, `/sbin/init` remains PID 1, repeatedly spawning `/bin/login`, waiting for it, and restarting login after exit.

PID-1 behavior is a property of a configured pure-POSIX domain, not intrinsically of domain 1.

## Verification

Tests must explicitly distinguish **generic mechanism** from **current configuration**.

### Generic mechanism tests

Verify that:

* namespace/view construction does not depend on domain number;
* a synthetic POSIX view can be constructed for an arbitrary test domain;
* a native namespace can be constructed for an arbitrary test domain;
* native and POSIX views can coexist within the same arbitrary domain;
* a pure-POSIX configuration can be constructed for an arbitrary domain;
* process ABI/profile selection follows executable resolution and configured view, not domain ID;
* domain-local services are owned by the relevant `dominit`;
* `dominit0` remains limited to explicitly delegated primitive/root-task mechanisms;
* native processes do not acquire `IPOSIXView` unless explicitly configured;
* POSIX processes do not acquire native filesystem or privileged process/auth/security capabilities;
* two domains with POSIX views maintain independent namespace/CWD/session state.

Add tests specifically intended to fail if implementation code introduces assumptions equivalent to:

```c
if (domain_id == 0) {
    /* native */
}

if (domain_id == 1) {
    /* POSIX */
}
```

### Current configuration tests

For the checked-in domain-0 configuration verify:

* native root contains `/FacetOS`, `/Data`, `/home`, `/posix`, and expected native files;
* native root contains no physical `/bin` or `/etc`;
* `system.initrd` contains no `/etc`, `/posix/etc`, `/etc/passwd`, `/etc/shadow`, or `/etc/fstab`;
* its POSIX `/` exposes the contents of native `/posix` without exposing the `/posix` prefix;
* POSIX `/etc` exists only through its synthetic mount;
* synthetic `/etc` contents and permissions are correct.

For the checked-in domain-1 configuration verify:

* `child.initrd` contains its own real `/etc/passwd`, `/etc/shadow`, and `/etc/fstab`;
* its namespace does not depend on domain 0;
* `/sbin/init` receives PID 1;
* login, shell, spawn, wait, CWD, environment, terminal, and authentication work independently.

Test path semantics including:

* absolute lookup;
* relative lookup;
* CWD;
* `.` and `..`;
* root-boundary behavior;
* missing paths;
* file-vs-directory errors;
* directory enumeration;
* executable lookup through the same namespace resolver.

Test application behavior including:

* valid and invalid login;
* configured `posix_shell`;
* root `#` prompt;
* non-root `$` prompt;
* CWD in the prompt;
* `ls` and `cat` diagnostics;
* explicit rejection of unsupported shell syntax;
* child exit status;
* accurate terminal/domain banners.

## QEMU verification

Run:

* `make test`;
* `make build`;
* bounded `make run TIMEOUT='timeout …'` checks.

For serial automation, `ttyS0` may temporarily be reassigned for debugging, but every temporary change must be restored afterward.

Verify the current configuration:

1. Native `ttyS0`:

   * native login;
   * native namespace;
   * `/posix` visible;
   * no native `/bin` or `/etc`.

2. Native VGA `tty1`:

   * native login;
   * correct domain/terminal banner;
   * same configured native namespace semantics.

3. Domain-0 POSIX `tty2`, temporarily routed to serial when useful:

   * `/bin/login`;
   * correct domain/terminal banner;
   * root and ordinary-user login;
   * correct `#`/`$` prompts;
   * CWD;
   * `/bin/sh`, `/bin/ls`, `/bin/cat`;
   * synthetic `/etc`;
   * shadow permissions;
   * error cases;
   * no visible `/posix` prefix;
   * no undelegated native authority.

4. Current pure-POSIX domain-1 `tty3`, temporarily routed to serial when useful:

   * PID 1 `/sbin/init`;
   * login respawn;
   * real domain-local `/etc`;
   * shell and commands;
   * process IDs;
   * spawn/wait;
   * CWD;
   * environment;
   * terminal propagation;
   * isolation.

Finally restore and verify the checked-in layout:

```text
ttyS0 → domain 0 native
tty1  → domain 0 native
tty2  → domain 0 POSIX view
tty3  → domain 1 pure POSIX
```

This verifies the current configuration; it must not define the architecture.

## Commit sequence

First make one baseline commit containing the current tracked and new work after:

* confirming the tree builds;
* confirming tests in the current baseline state;
* running `git diff --check`.

Then make logical commits:

1. `initrd: separate configured native and POSIX backing trees`
2. `dominit: implement generic domain-local POSIX namespace views`
3. `posix: complete login shell and command behavior`
4. `tests: verify namespace authority configuration and terminal behavior`

Do not collapse later work into a single final cleanup commit unless changes are inseparable.

## Architectural invariants

* A domain ID identifies a domain; **it does not identify a personality**.
* Domain 0 is not intrinsically the native domain.
* Domain 1 is not intrinsically the POSIX domain.
* The current domain-0 native+POSIX and domain-1 pure-POSIX arrangements are configuration choices only.
* `dominit0` provides privileged mechanism; each `dominit` owns its domain-local policy.
* A domain may expose native interfaces, POSIX views, both, neither, or future execution/personality environments according to configuration and delegated authority.
* Multiple domains may independently expose equivalent personality types without sharing mutable namespace/session state.
* Domain namespace construction belongs to `dominit`.
* Every capability supplied to a user process is explicitly selected and delivered through its process environment.
* Native and POSIX namespaces are separate views even when they coexist in one domain.
* A POSIX view's root and mounts are properties of that configured view, not universal FacetOS paths.
* The current domain-0 POSIX `/` happens to project native `/posix`; another domain's POSIX view need not use that backing arrangement.
* The current domain-0 POSIX `/etc` happens to be synthetic; another configured POSIX view may use a real filesystem or another provider.
* The current pure-POSIX domain 1 happens to have a real `/etc`; that is not a property of pure-POSIX domains in general.
* POSIX executable lookup uses the same configured namespace model as POSIX file access.
* POSIX applications receive no implicit native authority.
* `dominit0` must not accumulate domain-specific namespace, authentication, shell, POSIX, VM, or other personality policy merely because it possesses the root authority needed to provide lower-level mechanisms.
* Hybrid applications are deferred, but the architecture must not prevent a future process from being explicitly given both native interfaces and one or more personality views.
