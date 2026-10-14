"""Last-round key recovery on a capture. THE PART THAT DECIDES.

    python tools/attack.py results/stm32l152re/*.npz --line-shifts 3,1
    python tools/attack.py results/stm32f429zi/*.npz --line-shifts 4,2

A STATISTIC IS NOT A CHANNEL. tools/capture.py reports a spread; a spread
can be large and yield nothing. This reports whether the exact master key
falls out, and at how many traces.

TWO PAIR MODELS, AND NEITHER DOMINATES. Both are run by default and the
summary says which one won.

  `24`   the intra-column byte pairs reachable from six row pairings,
         unioned, both adjacencies swept.
  `120`  every output-byte pair, with each key byte voted over the 15
         that touch it.

  This file originally ran only the narrow one, and under it the two
  third-party libraries here recovered nothing while the reference cipher
  gave the exact key at 5 000 traces. The obvious reading was that they
  need more traces. **That reading is wrong**, and it is wrong in a way
  the data itself settles: a real effect's margin is in cycles and holds
  its absolute size as n grows, while a noise margin falls as 1/sqrt(n).
  Quadrupling the data halves the dead pair sets to within one per cent
  and does not move the live ones -- so the signal is already saturated
  and the missing bytes are not starved of data.

  What was missing is pairs the model cannot express. About half of these
  libraries' consecutive round-10 reads CROSS a column boundary, and the
  24-pair model reports those as absent. Scanning all 120 takes the same
  captures to the exact key -- Mbed-TLS with ROM tables at 2 500 traces,
  from a capture the narrow model scored 0/16 on at 20 000.

  But the narrow model is not merely a worse version of the wide one: on
  the reference cipher it reaches the exact key from fewer traces than
  the 120-pair vote, which at the same count gets 14/16. So both run.

THE CONTROLS ARE THE POINT, NOT AN AFTERTHOUGHT. A 120-pair vote has five
times the freedom of a 24-pair one, so it can manufacture a recovery where
the narrower model found none. Every bit-exactly constant build and every
RAM twin is therefore scored with the IDENTICAL call, and a control that
recovers anything invalidates the run rather than the build. `--controls`
lists what was treated as one; the summary refuses to stand if any of them
scored.

TWO GEOMETRY FACTS, both of which return a clean null when wrong rather
than a degraded answer:

  line_shift   log2 of the flash read width in BYTES **divided by the
               entry size of the table the last round reads**. On a part
               with an 8-byte read that is 3 for a byte S-box and 1 for a
               4-byte word table -- and WHICH of the two a configuration
               reads is a property of the library's last round, not of the
               part. So it is swept, not predicted.

  line_off     the table's offset within its row, from the address the
               DEVICE reported and the capture recorded. A trace count
               quoted without it is under-specified.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import pathlib
import sys
import time

# Be a good citizen: the recovery is the CPU-expensive half and this box
# also drives the bench. Set before numpy is imported or it has no effect.
_T = "2"
if "--threads" in sys.argv:
    _T = sys.argv[sys.argv.index("--threads") + 1]
for _v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
           "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ.setdefault(_v, _T)

import numpy as np                                            # noqa: E402

_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "flashwhisperer"))

from flashattack import aesref, engine                        # noqa: E402
from flashattack.lastround import (ROW_PAIRINGS, recover_k10,  # noqa: E402
                                   invert_ks)

#: The victim's compiled-in key. Used only to SCORE; never sent anywhere.
KEY = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")
_K10 = list(np.asarray(aesref.key_expansion(KEY),
                       dtype=np.uint8).reshape(-1)[-16:])


def model24(ct, y, n, line_shift, line_off):
    """The 24 INTRA-COLUMN byte pairs: six row pairings, unioned, with each
    key byte taken from whichever pairing scored the largest margin on it.
    Both last-round adjacencies swept, because a byte S-box build reads in
    state order while a T-table build that folds ShiftRows emits
    ciphertext-adjacent reads, and guessing wrong returns a clean null.

    NEITHER MODEL DOMINATES, which is why both are run. This one is
    narrower and, on the captures here, recovers the reference cipher's
    exact key from fewer traces than the 120-pair vote does. The wide one
    is what reaches the third-party libraries at all, because about half
    of their consecutive round-10 reads cross a column boundary and this
    model cannot express those pairs.
    """
    best = (0, False, None, None)
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
        got = sum(1 for i in merged if merged[i] == _K10[i])
        exact, master = False, None
        if len(merged) == 16:
            master = bytes(invert_ks({i: merged[i] for i in range(16)}))
            exact = (master == KEY)
        arm = "ct" if ct_adjacent else "state"
        if (exact, got) > (best[1], best[0]):
            best = (got, exact, master, arm)
    return best            # (bytes_correct, exact, master, adjacency)


def run_one(path: pathlib.Path, ladder, line_shifts, line_offs, models):
    d = np.load(path, allow_pickle=False)
    meta = json.loads(str(d["meta"]))
    pt, ct = d["pt"], d["ct"]
    cyc = d["cyc"].astype(np.float64)

    tab = meta["table_addr"]
    uniq = len(np.unique(cyc))
    sd = float(cyc.std(ddof=1)) if len(cyc) > 1 else 0.0

    # A build whose on-chip count is bit-exactly constant is a CONTROL: it
    # has nothing to recover from, and if the vote finds something in it
    # the vote is not trustworthy on this run.
    is_control = (uniq == 1) or meta.get("tables_in_ram", False)

    print("\n== %s%s" % (path.name, "   [CONTROL]" if is_control else ""))
    print("   %s %s [%s]  table 0x%08x  n=%d  sd %.4f  distinct %d"
          % (meta["lib"], meta["lib_version"], meta["cfg_str"], tab,
             len(cyc), sd, uniq))

    if uniq == 1:
        print("   one distinct cycle count: nothing to recover from, and for"
              " a tables-in-RAM build that IS the expected result.")
        return [dict(variant=meta["variant"], lib=meta["lib"],
                     cfg=meta["cfg_str"], table=tab, n=0, line_shift=None,
                     line_off=None, model=None, sd=sd,
                     bytes_correct=0, exact=False, control=True,
                     note="constant")]

    rows = []
    for n in ladder:
        if n > len(cyc):
            break
        for ls in line_shifts:
            # The table's offset within its flash row. Derived from the
            # recorded address by default, which is right for a table the
            # linker aligned -- but a library's own table can land
            # anywhere, and the offset is worth 2x to 10x in traces, so it
            # is sweepable. `--line-offs auto` keeps the derivation.
            if line_offs is None:
                offs = [tab & ((1 << ls) - 1)]
            else:
                offs = [o for o in line_offs if o < (1 << ls)]
            for line_off in offs:
                for model in models:
                    t0 = time.time()
                    if model == "120":
                        res = engine.recover(
                            ct[:n], cyc[:n], direction="enc", keysize=128,
                            line_shift=ls, line_off=line_off, truth=_K10,
                            verify=(bytes(pt[0]), bytes(ct[0])))
                        got = int(res.truth_correct)
                        exact = (res.master == KEY)
                        extra = "bf=%s" % list(res.bruteforced)
                    else:
                        got, exact, _m, arm = model24(ct, cyc, n, ls, line_off)
                        extra = "adj=%s" % arm
                    tag = "EXACT MASTER KEY" if exact else "-"
                    print("     n=%-7d ls=%d off=%-2d %-3s %2d/16  %-14s %s"
                          "  (%.0fs)"
                          % (n, ls, line_off, model, got, extra, tag,
                             time.time() - t0))
                    rows.append(dict(variant=meta["variant"], lib=meta["lib"],
                                     cfg=meta["cfg_str"], table=tab, n=n,
                                     line_shift=ls, line_off=line_off,
                                     model=model, sd=sd, bytes_correct=got,
                                     exact=bool(exact), control=is_control))
                    if is_control and (exact or got > 4):
                        print("   *** A CONTROL RECOVERED SOMETHING. The %s"
                              "-pair model is not trustworthy on this run."
                              " ***" % model)
        if any(r["exact"] for r in rows if r["n"] == n):
            break
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("npz", nargs="+")
    ap.add_argument("--ladder", default="5000,20000,50000,100000,200000")
    ap.add_argument("--line-shifts", default="3,1",
                    help="log2(flash read bytes / table entry bytes), swept. "
                         "8-byte read: 3 for a byte S-box, 1 for a 4-byte "
                         "word table. 16-byte read: 4 and 2.")
    ap.add_argument("--line-offs", default="auto",
                    help="the table's offset within its flash row. `auto` "
                         "derives it from the recorded address, which is "
                         "right for a linker-aligned table; a comma list "
                         "sweeps it, which is worth 2x to 10x in traces for "
                         "a library table that landed at an arbitrary offset")
    ap.add_argument("--models", default="120,24",
                    help="which pair models to score. NEITHER DOMINATES: "
                         "`24` (intra-column) reaches the reference cipher "
                         "in fewer traces, `120` (all output-byte pairs) is "
                         "what reaches the third-party libraries at all")
    ap.add_argument("--threads", default="2")
    ap.add_argument("--out")
    a = ap.parse_args(argv)

    paths = []
    for pat in a.npz:
        paths.extend(sorted(pathlib.Path(p) for p in glob.glob(pat)))
    if not paths:
        raise SystemExit("no captures matched")

    ladder = [int(x) for x in a.ladder.split(",")]
    shifts = [int(x) for x in a.line_shifts.split(",")]
    offs = (None if a.line_offs.strip() == "auto"
            else [int(x, 0) for x in a.line_offs.split(",")])
    models = [m.strip() for m in a.models.split(",")]

    rows = []
    for p in paths:
        rows.extend(run_one(p, ladder, shifts, offs, models))

    # ---- the summary, and the control check that gates it ---------------
    bad = [r for r in rows if r["control"] and (r["exact"] or r["bytes_correct"] > 4)]

    print("\n== summary (line_shifts=%s, models=%s) =="
          % (a.line_shifts, a.models))
    print("   %-40s %-14s %7s %6s %4s %s"
          % ("variant", "cfg", "sd", "n", "ls", "result"))
    def rank(r):
        """Best row for a variant: an exact key beats anything, then more
        correct bytes, then the SMALLEST n that achieved it."""
        return (r["exact"], r["bytes_correct"], -r["n"])

    best = {}
    for r in rows:
        k = r["variant"]
        if best.get(k) is None or rank(r) > rank(best[k]):
            best[k] = r

    for k in sorted(best):
        r = best[k]
        if r.get("note") == "constant":
            note = "constant -- control"
        elif r["exact"]:
            note = "EXACT at n=%d ls=%d, %s-pair" % (
                r["n"], r["line_shift"], r["model"])
        else:
            note = "%d/16 (best, n=%d ls=%d %s-pair)" % (
                r["bytes_correct"], r["n"], r["line_shift"], r["model"])
        print("   %-40s %-14s %7.4f %6s %4s %s"
              % (k[:40], r["cfg"], r["sd"], r["n"] or "-",
                 r["line_shift"] if r["line_shift"] is not None else "-",
                 note))

    if bad:
        print("\n   *** THIS RUN IS NOT TRUSTWORTHY ***")
        for r in bad:
            print("   a control recovered %d/16: %s" % (r["bytes_correct"],
                                                        r["variant"]))
        print("   A 120-pair vote has five times the freedom of a 24-pair")
        print("   one. If it scores on a build with nothing to find, it is")
        print("   manufacturing, and no row above may be quoted.")
    else:
        ncon = len({r["variant"] for r in rows if r["control"]})
        print("\n   controls clean: %d bit-exactly constant or RAM-twin "
              "builds scored nothing under the identical call." % ncon)

    if a.out:
        pathlib.Path(a.out).write_text(json.dumps(rows, indent=2))
        print("   wrote %s" % a.out)
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
