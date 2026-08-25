# FacetOS POSIX bootstrap slice

`PosixLogin` is the first executable using the FacetOS POSIX ABI. Its custom
x86-64 SysV `_start` loads the seL4 runtime environment, consumes the four
Facet bootstrap arguments, obtains the process-specific `IPOSIXView`, and only
then calls the program's C `main()`.

The target deliberately omits the normal `crt0.o`, `crti.o`, and `crtn.o`.
Its assembly entry point therefore also supplies complete no-op `_init` and
`_fini` hooks required by sel4runtime; without those hooks the archive linker
can extract `crti.o`'s prologue without the unreferenced `crtn.o` epilogue.

The public libc surface currently contains only `write()`. Add future libc
wrappers beneath `libc/` and corresponding transport methods to
`idl/IPOSIXView.facet`. Server implementations belong in dominit0's POSIX-view
factory and should keep narrower streams, files, credentials, and page
allocators private rather than exposing them in a pure-POSIX environment.
