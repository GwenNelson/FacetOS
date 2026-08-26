#!/usr/bin/env python3
"""Keep ordinary POSIX application sources independent of Facet interfaces."""

from pathlib import Path
import re
import sys


FORBIDDEN = ("facetos/", "facet_posix_", "libfacet/", "seL4", "sel4_")
# libc-posix is the sole ABI boundary.  Do not allow an application to avoid
# that boundary by naming generated interfaces or generic Facet runtime types
# directly, even if the source did not include one of the headers above.
FORBIDDEN_IDENTIFIERS = re.compile(
    r"\b(?:IPOSIXView|IProcessEnvironment|IProcessManager|"
    r"FacetHandle|FacetResult|FacetRpcMessage|facet_[A-Za-z0-9_]*)\b"
)
LOGIN_REQUIRED = (
    "getpwnam(", "getspnam(", "crypt(", "setgid(", "setuid(",
    "chdir(", "posix_spawn(", "waitpid(", "gethostname(", "ttyname(",
)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: posix_api_boundary_test.py POSIX_APP_DIRECTORY")
    directory = Path(sys.argv[1])
    for source in sorted(directory.glob("*.c")):
        text = source.read_text()
        for token in FORBIDDEN:
            if token in text:
                raise SystemExit(f"{source}: POSIX application uses forbidden {token}")
        match = FORBIDDEN_IDENTIFIERS.search(text)
        if match is not None:
            raise SystemExit(
                f"{source}: POSIX application uses direct Facet ABI identifier "
                f"{match.group(0)}"
            )
    login = directory / "login.c"
    login_text = login.read_text()
    for call in LOGIN_REQUIRED:
        if call not in login_text:
            raise SystemExit(f"{login}: conventional login missing {call}")
    print("POSIX application API boundary tests passed")


if __name__ == "__main__":
    main()
