# FacetOS POSIX bootstrap slice

`PosixLogin` and `/bin/test_perms` use the FacetOS POSIX ABI. Their custom
x86-64 SysV `_start` loads the runtime environment, reads the versioned
`AT_FACET_*` bootstrap entries from auxv, obtains the process-specific
`IPOSIXView`, and only then calls the program's standard C `main(argc, argv)`.
Capability bootstrap data is never encoded in `argv`.

The target deliberately omits the normal `crt0.o`, `crti.o`, and `crtn.o`.
Its assembly entry point therefore also supplies complete no-op `_init` and
`_fini` hooks required by sel4runtime; without those hooks the archive linker
can extract `crti.o`'s prologue without the unreferenced `crtn.o` epilogue.

The owned POSIX libc currently supplies environment/auxv access, errno,
allocation, `read`, `write`, `open`, `close`, `lseek`, basic stdio, and process
exit. Every operation translates through the single `IPOSIXView`; a pure-POSIX
process receives no native process namespace. Add future wrappers beneath
`libc/` and matching transport methods to `idl/IPOSIXView.facet`. Server
implementations belong in dominit0's POSIX-view factory and keep streams,
files, credentials, and page allocators private.

The CRT and platform transport necessarily contain the seL4 startup/client
backend. Application source and both libc archives do not call seL4 APIs. The
seat servers are intentionally separate platform services: their hardware
backends under `src/seat/platform/` still use seL4 I/O-port capabilities.
