"""Score LibTomCrypt's word-table AES captures with its actual pair geometry.

The generic library sweep tries all pairs or within-column pairs. The default
LibTomCrypt final round instead uses four separate Te4_* word tables: only
output bytes in the same byte lane index the same table. LTC_SMALL_CODE uses
one shared Te4 word table, so all output-byte pairs are eligible.

    python tools/attack_libtomcrypt.py results/stm32f429zi/libtomcrypt*.npz
"""
from __future__ import annotations

import argparse
import glob
import json
import pathlib
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "flashwhisperer"))
from flashattack import aesref, engine  # noqa: E402
from attack import auto_line_off, row_bytes_for  # noqa: E402

KEY = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")
K10 = bytes(aesref.key_expansion(KEY)[10])
SAME_LANE = [(a, b) for a in range(16) for b in range(a + 1, 16)
             if a % 4 == b % 4]


def score(path: pathlib.Path, rungs: list[int]) -> bool:
    with np.load(path, allow_pickle=False) as d:
        meta = json.loads(str(d["meta"]))
        pt, ct, cyc = d["pt"], d["ct"], d["cyc"]
    if meta.get("lib") != "libtomcrypt":
        raise ValueError(f"{path}: not a LibTomCrypt capture")
    if len(cyc) == 0 or len(pt) != len(cyc) or len(ct) != len(cyc):
        raise ValueError(f"{path}: incomplete capture")

    row = row_bytes_for(meta["mcu"])
    if row < 4 or row & (row - 1):
        raise ValueError(f"{path}: unsupported flash row width {row}")
    shift = (row // 4).bit_length() - 1
    # The linker puts the forward tables in one contiguous 1 KB-aligned
    # slot. Te4 begins after whole 1 KB tables, so its row offset matches
    # the slot's first table for measured row widths up to 1 KB.
    if row > 1024:
        raise ValueError(f"{path}: inspect Te4 address for row width {row}")
    table = int(meta["tables"][1])
    off = auto_line_off(table, row, shift)
    small = bool(int(meta["cfg"]) & 0x04)  # SS_LC_FEWER = LTC_SMALL_CODE
    pairs = "all" if small else SAME_LANE
    control = bool(meta["tables_in_ram"])

    print(f"{path.name}: {'small' if small else 'default'} "
          f"{'RAM control' if control else 'flash'}; "
          f"n={len(cyc)}, mean={cyc.mean():.2f}, "
          f"word-row shift={shift}, offset={off}, "
          f"pairs={120 if small else len(SAME_LANE)}", flush=True)
    exact = False
    for n in rungs:
        if n > len(cyc):
            continue
        res = engine.recover(
            ct[:n], cyc[:n], direction="enc", keysize=128,
            line_shift=shift, line_off=off, pairs=pairs,
            verify=(pt[0].tobytes(), ct[0].tobytes()), truth=K10,
            max_bruteforce=0,
        )
        print(f"  {n:6} traces: {res.truth_correct:2}/16 "
              f"verified={res.verified} brute={len(res.bruteforced)}", flush=True)
        exact |= bool(res.verified and res.master == KEY)
    if control and exact:
        print("  FAIL: RAM-table control verified a key", file=sys.stderr)
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("captures", nargs="+", help="capture .npz files or globs")
    ap.add_argument("--ladder", default="2500,5000,10000,25000,50000")
    args = ap.parse_args()
    paths = sorted({pathlib.Path(p) for pattern in args.captures for p in glob.glob(pattern)})
    if not paths:
        ap.error("no captures matched")
    rungs = [int(x) for x in args.ladder.split(",")]
    return 0 if all(score(path, rungs) for path in paths) else 1


if __name__ == "__main__":
    raise SystemExit(main())
