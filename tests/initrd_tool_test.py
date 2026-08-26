#!/usr/bin/env python3
import pathlib
import subprocess
import sys
import tempfile


tool = pathlib.Path(sys.argv[1]).resolve()


def run(*arguments, ok=True):
    result = subprocess.run([tool, *arguments], check=False,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if (result.returncode == 0) != ok:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="facet-initrd-test-") as temporary:
    root = pathlib.Path(temporary)
    source = root / "source"
    overlay = root / "overlay"
    (source / "etc").mkdir(parents=True)
    (overlay / "opt").mkdir(parents=True)
    (source / "etc" / "value").write_text("one\n")
    (overlay / "opt" / "extra").write_text("two\n")
    first = root / "first.cpio"
    second = root / "second.cpio"
    run("pack", first, source, "--overlay", overlay)
    run("pack", second, source, "--overlay", overlay)
    assert first.read_bytes() == second.read_bytes()
    run("chmod", first, "0600", "etc/value")
    run("chown", first, "1000:1000", "etc/value")
    listing = run("list", first)
    assert "0100600 1000:1000" in listing
    extracted = root / "extracted"
    run("unpack", first, extracted)
    assert (extracted / "etc" / "value").read_text() == "one\n"
    replacement = root / "replacement"
    replacement.write_text("changed\n")
    run("add", first, replacement, "etc/value", "--replace")
    run("remove", first, "opt")
    listing = run("list", first)
    assert "opt/extra" not in listing
    assert "changed" not in listing
    run("add", first, replacement, "../escape", ok=False)

print("initrd tool tests passed")
