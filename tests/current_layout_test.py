#!/usr/bin/env python3
"""Verify the checked-in native+POSIX and pure-POSIX initrd layouts."""

import subprocess
import sys


def entries(tool: str, image: str) -> set[str]:
    output = subprocess.check_output([tool, "list", image], text=True)
    return {line.split(maxsplit=3)[3] for line in output.splitlines() if line}


def require(contents: set[str], path: str, image: str) -> None:
    if path not in contents:
        raise SystemExit(f"{image}: missing {path}")


def reject(contents: set[str], path: str, image: str) -> None:
    if path in contents:
        raise SystemExit(f"{image}: must not contain physical {path}")


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: current_layout_test.py TOOL SYSTEM_INITRD CHILD_INITRD")
    tool, system_image, child_image = sys.argv[1:]
    system = entries(tool, system_image)
    child = entries(tool, child_image)

    for path in ("FacetOS", "Data", "home", "home/root", "home/user",
                 "posix", "posix/bin", "posix/home", "posix/home/root",
                 "posix/home/user", "Data/TestData/root-private",
                 "Data/TestData/root-private/inside.txt", "README"):
        require(system, path, system_image)
    for path in ("posix/bin/login", "posix/bin/sh", "posix/bin/ls", "posix/bin/cat"):
        require(system, path, system_image)
    for path in ("bin", "etc", "etc/passwd", "etc/shadow", "etc/fstab", "posix/etc"):
        reject(system, path, system_image)

    for path in ("bin", "sbin", "sbin/init", "etc", "etc/passwd",
                 "etc/shadow", "etc/fstab", "home", "home/root",
                 "home/user", "usr/share/test_data/root-private",
                 "usr/share/test_data/root-private/inside.txt"):
        require(child, path, child_image)
    print("current initrd layout tests passed")


if __name__ == "__main__":
    main()
