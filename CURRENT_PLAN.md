# Current FacetShell cwd and path-resolution status

This file records the implementation verified on 2026-08-25. The accepted
FacetShell current-directory and initrd path-resolution plan is complete.

## Filesystem object model

- An `IDirectory` object can report its canonical absolute `path` and open a
  file or directory relative to itself. An absolute argument starts at the
  root of the same store.
- `IFileStore.open_file()` and `open_directory()` use the same resolver, with
  relative arguments interpreted from `/`.
- Adding the three `IDirectory` operations changed its auto-generated UUID.
  dominit0, FacetShell, tests, and all generated contracts are rebuilt
  together; the existing `list` method remains ordinal 100.
- Cwd is not mutable state in the domain-wide store. FacetShell owns an
  `IDirectory` capability for its cwd. A future `IPOSIXView` can privately own
  the same lower-level object without exposing it to a pure-POSIX process.
- `IPOSIXView` was not changed by this work.

## Path semantics

- Runtime lookup accepts absolute and relative paths, repeated separators,
  trailing separators for directories, `.` components, and `..` components.
- `..` at `/` remains at `/`; resolution cannot escape the delegated store.
- Resolved paths are canonical absolute paths with no trailing slash except
  for `/`.
- Empty strings, embedded NULs, and paths whose normalized result exceeds the
  4096-byte service limit are rejected. A file lookup ending in `/`, `/.`, or
  `/..` is rejected because that syntax requires a directory.
- Trusted executable lookup through `facet_initrd_find_file()` remains
  absolute-only. CPIO archive-name validation remains strict.
- Nested directory enumeration no longer treats the directory itself as a
  child or reads beyond its path terminator. This fixes the corrupt output
  formerly produced by `ls /FacetOS`.

## FacetShell behavior

- Every shell starts with cwd `/`; bare `cd` also selects `/` until HOME and
  POSIX environment support exist.
- The prompt displays cwd, for example `/> ` and `/FacetOS> `.
- `pwd`, `ls [path]`, `cat <path>`, and `cd [path]` all use the directory
  object API. FacetShell does not join or normalize paths itself.
- `ls` without a path lists cwd. Failed `cd` reports an error and retains the
  previous directory.
- Verified forms include `cat README`, `cat ./README`, `ls .`, `ls ..`,
  `ls /FacetOS`, `ls /FacetOS/`, `cd FacetOS`, `cd ..`, and
  `cd /FacetOS/../`.
- The filesystem remains read-only and the shell retains its existing simple
  single-argument command syntax.

## Verification completed

- `CCACHE_DISABLE=1 make test` passes, including RPC tests for root-relative
  store lookup, directory-relative and absolute lookup, canonical paths,
  `.`, `..`, root clamping, repeated/trailing separators, type mismatches,
  malformed strings, length limits, file trailing-slash rejection, and nested
  directory listing.
- `CCACHE_DISABLE=1 make build` and `CCACHE_DISABLE=1 make image` pass.
- A bounded KVM/QEMU run was invoked through Make with an explicit timeout:
  `CCACHE_DISABLE=1 make run TIMEOUT="timeout 25s"` plus a headless console
  override for automated serial input.
- The guest serial transcript confirms root login, cwd-aware prompts, both
  relative `cat` forms, both `/FacetOS` `ls` forms, the three expected native
  executables, relative `cd`, root-clamped `..`, and canonical `pwd` output.
  QEMU exited with the expected timeout status 124 after the transcript.

## Deliberate later work

- Add POSIX open-file descriptions, descriptor tables, and per-process cwd to
  the private server-side implementation of `IPOSIXView`.
- Add writable filesystem providers, symlinks, mounts, permissions, and
  distinct POSIX errno mapping when those layers are designed.
- Replace bare native-shell `cd`-to-root behavior with a configured home
  directory once native process environment/home policy is available.
