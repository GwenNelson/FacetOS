  # Capability-driven namespaces and conventional POSIX login

  ## Summary

  Remove all native/POSIX process-profile and executable-classification logic. A process receives capabilities according to its parent and configured domain view; a program’s linked libc determines whether it uses native interfaces,
  IPOSIXView, or both.

  Domain 0’s native namespace exposes a synthetic read-only mount at /posix/etc; its IPOSIXView maps / to /posix, so the same mount appears as /etc. A pure POSIX domain’s IPOSIXView instead exposes its real initrd root and real /etc.

  ## Implementation changes

  - Replace password_sha256 with password_hash in standard crypt(3) format throughout config parsing, defaults, generated /etc/shadow, pure-POSIX initrd content, and authentication tests.
  - Rewrite /bin/login as conventional POSIX application code only:
      - use getpwnam, getspnam, crypt, setgid, setuid, chdir, posix_spawn, and waitpid;
      - remove Facet headers, Facet-named APIs, and direct interface access from every source in src/apps/posix;
      - use ordinary libc gethostname() and ttyname(0) wrappers for domain/terminal banner data.

  - Keep all IPOSIXView communication inside libc-posix. Implement its POSIX wrappers for identity changes and terminal/domain query; it updates the server-side inherited identity, CWD, and environment after login.
  - Delete Dominit0ProcessProfile, profile flags, and ABI/profile launch branches. Process construction becomes capability propagation:
      - children inherit the parent’s IPOSIXView whenever it has one;
      - native-capable parents additionally pass the native interfaces they were delegated;
      - POSIX-only parents pass only IPOSIXView and restricted runtime facilities;
      - native programs may use IPOSIXView when delegated, but need not do so.

  - Make native FacetShell launch children with its inherited IPOSIXView, allowing /posix/bin/* programs to observe / as /posix. No pathname-based or ELF-based program classification is performed.
  - Build a domain-local mounted filesystem namespace in dominit:
      - native domain: ordinary root plus synthetic /posix/etc/{passwd,shadow,fstab};
      - IPOSIXView for that domain: root is /posix, therefore the same mount is visible as /etc;
      - pure POSIX domain: root and /etc come from its own initrd with no /posix remapping.

  - Preserve /posix/etc as non-initrd, generated, read-only content. Native applications may inspect it through native filesystem interfaces; /posix/etc/shadow remains credential-restricted just as POSIX /etc/shadow is.

  ## Tests and verification

  - Strengthen the POSIX-source boundary test to reject all Facet headers, Facet-named APIs, generated interface headers, and direct runtime/interface use in POSIX application sources.
  - Test crypt-format configuration validation, conventional login success/failure, UID/GID transition, inherited credentials/environment, root/non-root prompts, and configured shells.
  - Test shared mount semantics:
      - native /posix/etc/{passwd,shadow,fstab};
      - POSIX /etc/{passwd,shadow,fstab};
      - no physical copies in either initrd;
      - root/non-root shadow access in both views.

  - Test every ls/cat combination from /, /etc, and nested CWDs with absolute paths, relative paths, ., .., missing paths, and wrong-type operands.
  - Test native FacetShell launching /posix/bin/{ls,cat,sh} and prove these children receive IPOSIXView with / mapped to /posix.
  - Run make test, make build, and bounded QEMU scenarios with make run TIMEOUT='timeout …' across native login, POSIX view login, and pure POSIX login.

  ## Assumptions

  - The password format migration is intentionally breaking: only password_hash values accepted by crypt(3) are supported.
  - /posix/etc is a mounted virtual filesystem shared by both domain-0 views; it is never physically stored in system.initrd.
  - The existing pure POSIX domain retains a real initrd /etc and does not use the domain-0 synthetic mount.
