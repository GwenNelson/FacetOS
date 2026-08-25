# Current implementation status

This file records the code that is present and verified as of 2026-08-25. The
isolated-seat, VGA-cursor, and first runnable POSIX-login plan is complete. The
remaining items below are deliberately later milestones rather than missing
parts of this slice.

## Configuration and seat ownership

- `[facet]` requires `seat_initrd`; the packaged and fallback configuration use
  `dominit0.initrd`.
- Every `[[seats]]` entry requires an absolute `server` path. The defaults are
  `/FacetOS/seat-server-serial` for `seat0` and
  `/FacetOS/seat-server-pc-console` for `seat1`.
- Parser coverage includes missing, empty, duplicate, and unknown seat-initrd
  and server fields.
- dominit0 creates the platform-neutral dynamic `current_seats` array. A
  `CurrentSeat` contains parsed configuration, remote seat/terminal handles,
  usability state, and opaque `void *platform_state`; it contains no seL4
  types.
- `dominit0.initrd` contains the two seat ELF files and is supplied by both the
  direct Multiboot and ISO/GRUB boot paths.
- dominit0 launches each configured seat through `platform_start_seat()`, waits
  on a shared badged ready/fault endpoint, discovers its configured remote
  terminals, and then assigns only those terminal capabilities to domains.
- A failed seat or missing terminal disables dependent assignments. Boot
  panics only when domain 0 has no usable configured terminal.

## Isolated seat services

- `seat-server-serial` owns COM1 ports `0x3f8-0x3ff` and implements
  `seat0.ttyS0`.
- `seat-server-pc-console` owns PS/2 ports `0x60-0x64`, VGA CRTC ports
  `0x3d4-0x3d5`, and the VGA text frame mapped at a seat-private address. It
  implements `seat1.tty1` through `seat1.tty5`.
- dominit0 derives and delegates the device capabilities and maps the VGA frame
  into the seat child. It does not execute serial, PS/2, or CRTC I/O and does
  not map the VGA frame into its own vspace.
- The compact single-threaded libfacet service backend exports multiple badged
  objects and transfers one capability per RPC without VKA or per-object
  server threads.
- Seat services convert the unused portion of their private 256 KiB bootstrap
  heap into a reclaimable liballoc region. This fixes the former per-RPC leak
  during empty input polling without giving either seat an `IPageAllocator` or
  any additional authority.

## VGA cursor behavior

- PC terminal buffers and cursor movement live in the platform-neutral
  `pc_console` core. Ordinary text, carriage return, newline, backspace, tab,
  scrolling, inactive terminals, and terminal selection are host-tested.
- CRTC access lives in `pc_cursor`. Initialization reads register `0x0a` and
  clears only its disable bit, preserving the firmware cursor shape.
- Every presentation of the active terminal copies its cells and writes the
  cursor cell offset to CRTC registers `0x0f` and `0x0e` (low byte, then high
  byte). Writes to inactive terminals do not touch VGA memory or the hardware
  cursor. Alt-F1 through Alt-F5 present the selected terminal immediately.

## Runnable POSIX write slice

- `IPOSIXView` currently provides only `write_fd(fd, array<u8>)`, returning a
  separate `FacetResult`, POSIX result, and errno value.
- Each pure-POSIX process gets its own view backed by its assigned private
  stdout writer. File descriptor 1 forwards to that writer; unsupported file
  descriptors return `-1` and `EBADF` with successful transport status.
- The process profile is selected from the authoritative configured terminal
  assignment. A pure-POSIX environment exposes only the `posix` binding and
  default `IPOSIXView`; direct stdin/stdout/stderr, logger, files, auth,
  security, process-manager, lifecycle, and page-allocator bindings are not
  discoverable.
- The child `IPageAllocator` is still constructed and retained as a private
  process dependency. This write-only executable uses its bounded bootstrap
  heap and does not yet request page-backed POSIX allocation.
- Domain 1 uses its local process manager and starts `/bin/login` on
  `seat1.tty2`. `child.initrd` contains the custom `bin/login` ELF.
- `src/posix` contains the x86-64 SysV `_start`, stack parsing, sel4runtime and
  libfacet bootstrap, `write()`, and `/bin/login`. The CRT consumes the four
  Facet bootstrap arguments, obtains `IPOSIXView` before calling C `main()`,
  and yields after `main()` because no POSIX exit operation exists yet.
- The POSIX link rule omits the normal `crt0.o`, `crti.o`, and `crtn.o`. The
  custom CRT supplies complete no-op `_init`/`_fini` hooks so sel4runtime cannot
  pull an unmatched `crti.o` prologue from its archive.

## Verification completed

- `make test` passes configuration/fallback, SHA-256, initrd, authentication,
  terminal/process-environment, POSIX-view, logging, and mocked PC-console/CRTC
  tests.
- `CCACHE_DISABLE=1 make build` builds the kernel, dominit0, dominit, both seat
  servers, native applications, custom `PosixLogin`, and all three initrds.
- The POSIX ELF is a static `ET_EXEC` whose entry point is its custom `_start`;
  `child.initrd` packages it as `bin/login`.
- Bounded KVM/QEMU boots show both independent seat servers, discovery of
  `seat0.ttyS0` and all five `seat1` terminals, domain 0 assignments to ttyS0
  and tty1, domain 1 assignment to tty2, and successful launch of all three
  configured initial processes.
- A QEMU monitor proof sends Alt-F2, reads the VGA frame as
  `hello from POSIX /bin/login`, and reads CRTC cursor position `0x0050`, the
  first cell of the following row. The native tty1 proof reads cursor position
  `0x0057`, matching its prompt exactly.

## Deliberate next milestones

- Expand `IPOSIXView` beyond `write()` (including exit, input, files, process,
  credentials, and time) while preserving the single-view authority boundary.
- Route POSIX heap growth through its private child `IPageAllocator` without
  making that allocator a discoverable process-environment binding.
- Replace polling PS/2 input with an interrupt-driven seat loop. At present,
  keyboard events are pumped by terminal input RPCs, which is sufficient while
  a login or shell is polling but is not the final driver architecture.
- Add a bounded seat-ready timeout. Startup faults are reported on the shared
  ready/fault endpoint, but a seat that neither faults nor announces readiness
  can still stall startup.
