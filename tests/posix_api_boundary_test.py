#!/usr/bin/env python3
"""Keep ordinary POSIX application sources independent of Facet interfaces."""

from pathlib import Path
import sys


FORBIDDEN = ("facetos/interfaces/", "facet_posix_runtime.h", "libfacet/")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: posix_api_boundary_test.py POSIX_APP_DIRECTORY")
    directory = Path(sys.argv[1])
    for source in sorted(directory.glob("*.c")):
        text = source.read_text()
        for token in FORBIDDEN:
            if token in text:
                raise SystemExit(f"{source}: POSIX application uses forbidden {token}")
    print("POSIX application API boundary tests passed")


if __name__ == "__main__":
    main()
