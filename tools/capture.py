"""Flash one build, read its configuration back off the device, and capture.

    python tools/capture.py --mcu stm32l152re --variant refsbox-sbox-ram0-vcp-... \\
                            --port COM3 --probe <serial> -n 20000

WHY IT READS THE CONFIGURATION OFF THE DEVICE rather than parsing the
directory name: the directory name is what someone MEANT to build, and
SS_V_LIBINFO is what is RUNNING. Those have disagreed before -- a rig
default flipped and fw.bin changed meaning without changing name. The saved
.npz records the device's answer, not the path.

WHAT IT CHECKS BEFORE BELIEVING ANY NUMBER, and it refuses to write a file
that fails any of them:

  * every ciphertext against a reference AES under the victim's compiled-in
    key. This is the check that matters most and the one a framing or
    length check would not do. In the rig this came from, a library framed
    correctly, returned AES_SUCCESS on every call, and produced a
    deterministic WRONG ciphertext for want of a peripheral clock nothing
    documents -- 60 000 traces of a mis-driven cipher would otherwise have
    been recorded as a measurement;
  * that the device reports a cycle counter before trusting a cycle column;
  * that the table address the device reports is not zero when the build
    was supposed to have tables in flash. A trace count quoted without the
    table's address is under-specified, so the address goes in the file.

A STATISTIC IS NOT A CHANNEL. This reports spreads; tools/attack.py runs
the recovery.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import time

import numpy as np

_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "instrumentation" / "host"))
sys.path.insert(0, str(_ROOT / "flashwhisperer"))

from mcueval import Victim, proto as P            # noqa: E402
from flashattack import aesref                     # noqa: E402

#: The victim's compiled-in key -- instrumentation/victim/src/ss_main.c.
#: Used only to SCORE and to content-check; the device never receives it.
KEY = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")


def port_for_probe(probe: str) -> str:
    """Resolve the serial port from the DEBUG PROBE'S SERIAL NUMBER, which
    is the only stable handle a board has.

    COM numbers and drive letters are assigned by the host and move on
    replug. This is not a theoretical concern: the registry this repository
    grew out of recorded COM3 for the part used to bring this tool up, the
    board was actually on COM53, and the stale value survived even that
    registry's own verifying accessor. Two boards of the same type are
    distinguishable ONLY by probe serial, so a copy or a capture addressed
    by anything else can land on the wrong board.
    """
    import serial.tools.list_ports as lp
    hits = [p.device for p in lp.comports()
            if (p.serial_number or "").upper() == probe.upper()]
    if not hits:
        seen = "\n".join("    %-8s %s" % (p.device, p.serial_number or "-")
                         for p in sorted(lp.comports(), key=lambda x: x.device))
        raise SystemExit("no serial port for probe %s. Attached:\n%s"
                         % (probe, seen))
    # A probe exposing several CDC interfaces (some debug probes expose two,
    # only one of which is wired to the target's UART) needs the caller to
    # say which. Refuse rather than guess: the wrong one is silent.
    if len(hits) > 1:
        raise SystemExit("probe %s exposes several ports (%s); pass --port"
                         % (probe, ", ".join(sorted(hits))))
    return hits[0]


def flash(probe: str, target: str, binary: pathlib.Path) -> None:
    """pyocd over SWD. Drag-and-drop to the ST-LINK volume also works and is
    the historical path, but it is SILENT ON FAILURE and cannot reset a
    wedged board without a reflash -- and on a host with two boards of the
    same type a copy to a bare drive letter can land on the wrong one."""
    # `python -m pyocd`, not the `pyocd` console script: the script is not
    # on PATH in every environment that has the package, and the failure is
    # a bare WinError 2 from CreateProcess that says nothing about pyocd.
    cmd = [sys.executable, "-m", "pyocd", "flash",
           "-t", target, "-u", probe, str(binary)]
    print("  $", " ".join(cmd[1:]))
    # pyocd prints check marks; a Windows console's default codec cannot
    # encode them and the run dies in its logger rather than in the flash.
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=300,
                       env=env)
    if r.returncode != 0:
        print(r.stdout[-2000:], file=sys.stderr)
        print(r.stderr[-2000:], file=sys.stderr)
        raise SystemExit("flash failed")


def capture(port: str, n: int, seed: int, baud: int, want_cyc: bool,
            acr=None):
    rng = np.random.default_rng(seed)
    pts = rng.integers(0, 256, size=(n, 16), dtype=np.uint8)

    with Victim(port, baud=baud) as v:
        fillers = v.resync()
        ident = v.ident()
        info = v.libinfo()
        print("  resync took %d filler bytes" % fillers)
        print("  ident  :", ident)
        print("  libinfo:", info)

        if acr is not None:
            # SS_V_SET_ACR replies with the READBACK, never the requested
            # value: the board layer refuses a configuration this clock
            # cannot run, and this is how a caller finds out. Refusing to
            # continue on a refusal matters -- a sweep that silently
            # captured the old setting under the new name would file every
            # point of the sweep under the wrong label.
            got = v.set_acr(acr)
            if got != acr:
                raise SystemExit(
                    "the device REFUSED accelerator word 0x%x and kept 0x%x.\n"
                    "That is the board layer declining a configuration this\n"
                    "clock cannot run. Nothing is captured." % (acr, got))
            ident = v.ident()
            print("  acr set:", ident)

        if want_cyc and not ident.reports_cyc:
            raise SystemExit(
                "this build reports no cycle counter, so there is no on-chip\n"
                "timing to capture. That is a real answer -- build with\n"
                "TIMING=systick, or measure it with the off-chip instrument.")

        cyc = np.zeros(n, dtype=np.uint32)
        cts = np.zeros((n, 16), dtype=np.uint8)
        t0 = time.time()
        for i in range(n):
            pt = bytes(pts[i])
            if want_cyc:
                ct, c = v.encrypt_cyc(pt)
                cyc[i] = c
            else:
                ct = v.encrypt(pt)
            cts[i] = np.frombuffer(ct, dtype=np.uint8)
            if (i + 1) % 2000 == 0:
                rate = (i + 1) / (time.time() - t0)
                print("    %6d/%d  %.0f enc/s" % (i + 1, n, rate), flush=True)

    return pts, cts, cyc, ident, info


def check_ciphertexts(pts, cts) -> None:
    """Every one of them, not a sample. A library that is being driven
    wrongly is usually wrong on every block, but one that is wrong only for
    some inputs is exactly the case a sample would miss."""
    want = aesref.encrypt_blocks(KEY, pts)
    mism = np.flatnonzero((want != cts).any(axis=1))
    bad = len(mism)
    if bad:
        i = int(mism[0])
        pt = bytes(pts[i]).hex()
        got = bytes(cts[i]).hex()
        want = bytes(want[i]).hex()
        raise SystemExit(
            "CIPHERTEXT MISMATCH on %d of %d traces. First at %d:\n"
            "  pt   %s\n  got  %s\n  want %s\n"
            "The victim is not computing AES-128 under the expected key.\n"
            "Nothing is written. A capture that fails this is a measurement\n"
            "of a mis-driven cipher, and it looks exactly like a real one."
            % (bad, len(pts), i, pt, got, want))
    print("  ciphertexts: all %d match the reference AES" % len(pts))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mcu", required=True)
    ap.add_argument("--variant", required=True,
                    help="a directory name under build/<mcu>/")
    ap.add_argument("--port", help="serial port; omit to resolve it from "
                                   "--probe, which is the stable handle")
    ap.add_argument("--probe", help="debug probe serial. Used to flash, and "
                                    "to resolve --port when that is omitted")
    ap.add_argument("--target", default="stm32l152retx", help="pyocd target")
    ap.add_argument("-n", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--acr", help="set the flash accelerator word before "
                                  "capturing, e.g. 0x705. The device replies "
                                  "with the readback and a refusal aborts")
    ap.add_argument("--no-cyc", action="store_true",
                    help="do not ask for the on-chip cycle count")
    ap.add_argument("--out", help="output .npz; default results/<mcu>/<variant>.npz")
    a = ap.parse_args(argv)

    build = _ROOT / "build" / a.mcu / a.variant
    binary = build / "fw.bin"
    if not binary.is_file():
        raise SystemExit("no such build: %s\nBuild it first with "
                         "./tools/build.sh" % binary)

    port = a.port
    if not port:
        if not a.probe:
            raise SystemExit("pass --port or --probe")
        port = port_for_probe(a.probe)

    print("== %s / %s on %s" % (a.mcu, a.variant, port))
    if a.probe:
        flash(a.probe, a.target, binary)
        # A probe run works right after a flash; the CDC port is freshly
        # enumerated and the device is at its reset vector.
        time.sleep(1.0)

    acr = int(a.acr, 0) if a.acr else None
    pts, cts, cyc, ident, info = capture(port, a.n, a.seed, a.baud,
                                         not a.no_cyc, acr)
    check_ciphertexts(pts, cts)

    if not a.no_cyc:
        uniq = len(np.unique(cyc))
        print("  cyc: mean %.3f  sd %.4f  min %d  max %d  distinct %d"
              % (cyc.mean(), cyc.std(ddof=1), cyc.min(), cyc.max(), uniq))

        # A COLUMN OF ZEROS IS A DEAD COUNTER, NOT A CONSTANT-TIME CIPHER,
        # and the two look identical in every summary statistic. Nothing
        # real encrypts a block in zero cycles, so this is cheap to tell
        # apart -- and worth telling apart loudly, because the first DWT
        # build brought up here produced exactly this while its own IDENT
        # reported a working counter. The counter had been armed at boot
        # and then switched off by the debug probe disconnecting.
        if uniq == 1 and int(cyc[0]) == 0:
            raise SystemExit(
                "EVERY CYCLE COUNT IS ZERO. That is a dead counter, not a\n"
                "constant-time cipher -- nothing encrypts a block in zero\n"
                "cycles. On Cortex-M this is usually DWT: DEMCR.TRCENA is\n"
                "cleared by the debug probe when it disconnects AFTER the\n"
                "board booted, so a counter armed in board init is armed\n"
                "and then turned off. Re-arm it somewhere that runs later.\n"
                "Nothing is written.")
        if uniq == 1:
            print("  NOTE: one distinct cycle count over the whole capture,")
            print("        and it is not zero. That is what a table in RAM")
            print("        looks like: a result, not a failure.")

    # The accelerator word goes in the FILENAME as well as the metadata:
    # two captures that differ only in it are different measurements, and
    # without it the second silently overwrites the first.
    stem = a.variant + ("-acr%03x" % ident.acr if acr is not None else "")
    out = pathlib.Path(a.out) if a.out else (
        _ROOT / "results" / a.mcu / (stem + ".npz"))
    out.parent.mkdir(parents=True, exist_ok=True)

    # The DEVICE's answer, not the path's. Everything needed to attribute a
    # trace count later lives in the file.
    meta = dict(
        mcu=a.mcu, variant=a.variant, n=a.n, seed=a.seed, baud=a.baud,
        port=port, probe=a.probe,
        lib=info.name, lib_version=info.version_str, cfg=info.cfg,
        cfg_str=P.cfg_str(info.cfg), build_byte=info.build_byte,
        table_addr=ident.table_addr, tables=list(info.tab),
        code_addr=info.code, acr=ident.acr, clk_hz=ident.clk_hz,
        layout=ident.layout, flags=ident.flags,
        tables_in_ram=ident.tables_in_ram, warmup=ident.warmup,
    )
    np.savez_compressed(out, pt=pts, ct=cts, cyc=cyc,
                        meta=json.dumps(meta))
    print("  wrote %s" % out)
    print("  meta: %s" % json.dumps(meta, indent=None))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
