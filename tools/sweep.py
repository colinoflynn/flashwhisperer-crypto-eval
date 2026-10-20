"""Build, flash and capture every configuration in a part's matrix.

    python tools/sweep.py --mcu stm32l152re --probe <serial> -n 20000
    python tools/sweep.py --mcu stm32l152re --probe <serial> --only wolfssl

One pass per entry: build it, flash it, read the configuration BACK OFF THE
DEVICE, capture, and verify every ciphertext before writing anything.

It builds with LINK=vcp so the victim's console comes out on the ST-LINK's
virtual COM port, which means a whole sweep needs no wiring and no second
board. That's the ON-CHIP arm, where the victim times its own encryption;
the off-chip arm needs the instrument board and its own harness.

ONE CONFIGURATION PER FLASH, AND THE FLASH IS NOT OPTIONAL. Several axes
here change the link-time layout, and the same firmware at three link-time
accidents has needed 6 250, 12 500 and 25 000 traces. Reusing a flashed
image across variants is how a capture gets filed under the wrong name.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import time

_ROOT = pathlib.Path(__file__).resolve().parents[1]

#: The matrix, as (library, extra make variables). Deliberately explicit
#: rather than a cross product: ROMTAB exists only for Mbed-TLS and FEWER
#: only for libraries that have it, so a generated product would build
#: meaningless combinations and report them as results.
MATRIX = [
    ("refsbox", dict(LAYOUT="sbox", RAMTAB="0")),
    ("refsbox", dict(LAYOUT="sbox", RAMTAB="1")),
    ("refsbox", dict(LAYOUT="ttable", RAMTAB="0")),
    ("mbedtls", dict(ROMTAB="0", RAMTAB="0")),
    ("mbedtls", dict(ROMTAB="1", RAMTAB="0")),
    ("mbedtls", dict(ROMTAB="1", RAMTAB="1")),
    ("mbedtls", dict(ROMTAB="1", FEWER="1", RAMTAB="0")),
    ("wolfssl", dict(FEWER="0", RAMTAB="0")),
    ("wolfssl", dict(FEWER="0", RAMTAB="1")),
    ("wolfssl", dict(FEWER="1", RAMTAB="0")),
    ("libtomcrypt", dict(FEWER="0", RAMTAB="0")),
    ("libtomcrypt", dict(FEWER="0", RAMTAB="1")),
    ("libtomcrypt", dict(FEWER="1", RAMTAB="0")),
]


_WSL_ROOT = None


def _wsl_root() -> str:
    """This repository's path as WSL sees it.

    Converted here rather than by calling `wslpath`: the backslashes in a
    Windows path are eaten crossing into `wsl --`, so wslpath receives
    `D:Dropboxacademic...` and fails. Quoting does not survive that boundary
    reliably either, so the conversion is done where the path still has its
    separators.
    """
    global _WSL_ROOT
    if _WSL_ROOT is None:
        p = pathlib.PureWindowsPath(_ROOT)
        drive = p.drive.rstrip(":").lower()
        if not drive:
            raise SystemExit("cannot map %s into WSL: no drive letter" % _ROOT)
        rest = "/".join(p.parts[1:])
        _WSL_ROOT = "/mnt/%s/%s" % (drive, rest)
    return _WSL_ROOT


def _make(args, goal=""):
    """Run build.mk in WSL, where the ARM toolchain lives. `bash -c` and a
    .sh file rather than `bash -lc` with the command inline: variables
    inside the login-shell form expand to empty."""
    cmd = ["wsl", "--", "bash", "-c",
           "cd %s && ./tools/build.sh %s %s"
           % (_wsl_root(), " ".join(args), goal)]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=900)


def build(mcu, lib, opts, link, baud):
    args = ["MCU=%s" % mcu, "LIB=%s" % lib, "LINK=%s" % link,
            "BAUD=%d" % baud] + ["%s=%s" % kv for kv in sorted(opts.items())]

    # ASK MAKE where this will land, rather than scraping the build log or
    # recomputing the naming rule here. A second copy of that rule is how a
    # capture gets filed under the wrong name - and the log says nothing at
    # all when the target is already up to date, which is exactly how this
    # failed the first time it ran.
    r = _make(args, "variant")
    if r.returncode != 0:
        print(r.stdout[-2000:], file=sys.stderr)
        print(r.stderr[-2000:], file=sys.stderr)
        raise SystemExit("could not resolve the variant for: %s" % " ".join(args))
    variant = r.stdout.strip().splitlines()[-1].strip()

    r = _make(args)
    if r.returncode != 0:
        print(r.stdout[-3000:], file=sys.stderr)
        print(r.stderr[-2000:], file=sys.stderr)
        raise SystemExit("build failed: %s" % " ".join(args))
    return variant


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mcu", default="stm32l152re")
    ap.add_argument("--probe", required=True)
    ap.add_argument("--target", help="pyocd target; default from mcufw/<mcu>/pyocd.target")
    ap.add_argument("--link", default="vcp")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("-n", type=int, default=20000)
    ap.add_argument("--only", help="substring filter on the library name")
    ap.add_argument("--out", default=None, help="JSON index of what was taken")
    a = ap.parse_args(argv)

    done, failed = [], []
    for lib, opts in MATRIX:
        if a.only and a.only not in lib:
            continue
        label = "%s %s" % (lib, " ".join("%s=%s" % kv for kv in sorted(opts.items())))
        print("\n########  %s" % label, flush=True)
        try:
            variant = build(a.mcu, lib, opts, a.link, a.baud)
            print("  variant %s" % variant, flush=True)
            # --target only when asked for: capture.py defaults it from the
            # part's own mcufw/<mcu>/pyocd.target, and passing None here
            # would hand it the string "None".
            cmd = [sys.executable, str(_ROOT / "tools" / "capture.py"),
                   "--mcu", a.mcu, "--variant", variant,
                   "--probe", a.probe,
                   "-n", str(a.n), "--baud", str(a.baud)]
            if a.target:
                cmd += ["--target", a.target]
            r = subprocess.run(cmd, timeout=3600)
            if r.returncode != 0:
                failed.append((label, "capture returned %d" % r.returncode))
            else:
                done.append(variant)
        except Exception as e:                                # noqa: BLE001
            # One configuration failing must not lose the rest of a sweep
            # that has already cost bench time.
            failed.append((label, repr(e)))
            print("  FAILED: %r" % e, file=sys.stderr, flush=True)

    print("\n== sweep done: %d captured, %d failed" % (len(done), len(failed)))
    for v in done:
        print("   ok   %s" % v)
    for label, why in failed:
        print("   FAIL %s - %s" % (label, why))
    if a.out:
        pathlib.Path(a.out).write_text(json.dumps(
            dict(mcu=a.mcu, n=a.n, link=a.link, baud=a.baud,
                 captured=done, failed=failed), indent=2))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
