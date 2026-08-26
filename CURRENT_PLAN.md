# Native Execution, FacetOS auxv ABI, Owned libc, Descriptor Translation, and Permissions

## Summary

- Replace hidden capability arguments in `argv` with FacetOS auxiliary-vector
  entries in the ELF `AT_LOOS`-`AT_HIOS` range.
- Make FacetShell execute foreground native programs, with quoted arguments
  and `PATH=/FacetOS`; move `ls` and `cat` to external binaries.
- Build owned `libc-native.a` and `libc-posix.a`, keeping application code free
  of seL4 and making POSIX calls translate only through `IPOSIXView`.
- Add descriptor objects, UID/GID permission enforcement, deterministic initrd
  tooling, standalone application build scripts, and permission test programs.

## Implementation status (2026-08-26)

This plan is implemented. FacetOS now boots the normal two-domain
configuration, authenticates `user`, launches FacetShell, executes external
programs with ordinary argv/envp plus the versioned auxv bootstrap, and passes
both native and pure-POSIX permission tests.

Host tests cover auxv validation/runtime lookup, shell quoting and PATH
candidates, authentication and credentials, path resolution, metadata,
credential-filtered files, repeated descriptor operations and errno, terminal
views, logging, and deterministic initrd transformations. Bounded QEMU checks
cover the process-boundary portions that host fakes cannot: clean application
argv, inherited cwd/env/session/stdio, foreground lifecycle completion, and the
seL4 capability transport.

The first special POSIX boot exposed a persistent-handle lifetime defect after
the test's START marker. The defect was reproduced in host RPC tests and fixed
by cloning borrowed handles before constructing temporary proxies. One bounded
post-fix retry produced `POSIX test_perms: PASS`. The default `facet.toml` was
then restored, rebuilt, and smoke-booted successfully. QEMU used TCG because
this execution environment did not expose `/dev/kvm`; all runs still went
through `make run` with an explicit `TIMEOUT`.

## FacetOS auxv and startup ABI

- Publish stable `AT_FACET_*` keys and the startup ABI version in common libc
  headers. Reserve common process, domain-bootstrap, and seat-server subranges
  beginning at `AT_LOOS`; reject duplicates, standard-key collisions, and
  values above `AT_HIOS`.
- Put the bootstrap root/object capability, receive CNode, slot, and depth in
  auxv. The SysV stack contains real argv, envp, standard/seL4 auxv entries,
  and FacetOS entries; no capability value is serialized in argv.
- Native CRT exposes the root through a native-only accessor. POSIX CRT obtains
  only `IPOSIXView`. Both libc variants implement `getauxval()`.
- Verify ordinary applications first, then migrate dominit and seat server
  bootstrap data to auxv. Legitimate application arguments remain in argv;
  dominit0's root-task ABI is unchanged. Document remaining seL4 seat backends.

## Interfaces and security model

- Repair `IProcessManager.launch` to accept path, argv, and the caller
  environment. The manager obtains cwd from that authenticated environment so
  callers cannot provide mismatched context. It inherits session, stdio, SysV
  environment, terminal, and cwd while creating fresh allocator and lifecycle
  objects.
- Add immutable `stdin`, `stdout`, `stderr`, and SysV environment state plus a
  mutable cwd to each process environment. Native processes may resolve their
  full namespace; pure POSIX processes receive only their `IPOSIXView`
  capability.
- Add `ICharacterStream`, `IFileDescriptor`, optional `IUnixMetadata`, canonical
  `IFile.path`, and process exit status. Extend `IPOSIXView` with descriptor
  read/write/open/close/seek, page allocation/free, and process exit.
- Keep a 64-entry POSIX descriptor table. Descriptors 0-2 wrap terminal streams;
  opened descriptors wrap credential-filtered files. The filesystem remains
  read-only and reports POSIX errno separately from RPC transport errors.
- Add configured UID, primary GID, home path, and optional terminal `run_as`.
  Root is UID/GID 0; add non-admin `user`, UID/GID 1000, password `facetos`,
  home `/home/user`. Enforce owner/group/other permissions and directory search.
  UID 0 and admins bypass read/write checks but still require an execute bit.

## Runtime, applications, filesystem, and tools

- Reorganize application sources under `src/apps/native/` and
  `src/apps/posix/`.
- `libc-native.a` and `libc-posix.a` own the SysV CRT, auxv/env/errno, required
  memory and string routines, allocator, `read`, `write`, `open`, `close`,
  `lseek`, `putchar`, `puts`, and exit. Both call standard
  `int main(int argc, char **argv)` and do not link musl.
- Move application bootstrap and yielding into application-facing
  `libfacet-platform-sel4.a`. Applications depend at project level only on one
  libc flavor, libfacet common, and that platform library.
- FacetShell keeps `help`, `whoami`, `pwd`, `cd`, and `exit` built in. It parses
  whitespace, quotes, and backslash escapes, searches colon-separated PATH,
  resolves slash paths from cwd, launches foreground children, and waits.
- `/FacetOS/ls` supports `-l`, `-a`, combined flags, `--`, and multiple paths.
  `/FacetOS/cat` supports `-n`, `--`, multiple files, and stdin via no operand
  or `-`.
- Preserve newc mode, UID, and GID through optional metadata objects and enforce
  access in credential-bound views. Add `/FacetOS/TestPerms` with data under
  `/Data/TestData`, and `/bin/test_perms` with data under
  `/usr/share/test_data`.
- Add native/POSIX GCC driver scripts, gitignored persistent initrd overlays,
  and a deterministic standalone newc tool supporting pack, unpack, list,
  add/replace, remove, chmod, and chown with atomic updates and path validation.

## Verification requirements

- Host tests cover auxv, shell parsing/PATH, credentials and permissions,
  descriptors and errno, metadata, and deterministic initrd round trips. QEMU
  covers clean argv, env/cwd inheritance, and process lifecycle across real
  address spaces.
- Inspect application link maps/symbols to confirm libc/app objects import
  neither musl nor seL4 APIs.
- Run normal tests/build/image and bounded QEMU with explicit `TIMEOUT`; verify
  external utilities, execution/path/quoting/cwd/env/auxv, user login, and
  native permission outcomes.
- Run a bounded image assigning child `/bin/test_perms` to `ttyS0` as
  `run_as = "user"`; verify POSIX startup, descriptors, file APIs, allocation,
  and denials. The initial diagnostic run found the handle-lifetime defect
  described above; the post-fix retry passed. Restore and rebuild the normal
  configuration, then perform a final bounded smoke boot.
- Preserve the unrelated untracked `TUTORIAL.md`.

## Deferred work

- Writable filesystems, supplemental groups, umask, fork, signals, pipes,
  redirection, expansion, globbing, and background jobs remain out of scope.
- Interface UUIDs remain `auto` while these ABIs are evolving. Published
  `AT_FACET_*` numbers are stable and extended through the ABI version.
