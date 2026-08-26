# Standalone application compiler drivers

Use `scripts/facet-native-gcc -o Program program.c` for a native application,
or `scripts/facet-posix-gcc -o program program.c` for a pure POSIX program.
Both drivers use FacetOS's SysV CRT and owned libc archive. They accept ordinary
GCC compile or link options; `-c`, `-E`, and `-S` automatically omit startup
objects and libraries.

Native programs may include Facet interfaces and receive the native process
root from auxv. POSIX programs intentionally link only the `IPOSIXView`
translation libc and cannot import native FacetOS capabilities.
