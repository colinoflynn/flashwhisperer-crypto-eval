"""Recover the key from captures taken by this bench. THE PART THAT DECIDES.

    python tools/attack.py results/stm32l152re/*.npz --line-shifts 3,1
    python tools/attack.py results/stm32f429zi/*.npz --line-shifts 4,2

A STATISTIC IS NOT A CHANNEL. tools/capture.py reports a spread; a spread
can be large and yield nothing. This reports whether the exact master key
falls out, and at how many traces.

THIS FILE IS AN ADAPTER AND NOTHING MORE, which is the point. The sweep
over trace counts, pair models and table geometry, and the control gate
that decides whether the run may be quoted, are all
`flashattack.sweep` -- because none of that is a property of this bench.
What stays here is only what is: how to read this repository's .npz
metadata, which captures are controls, and the victim's compiled-in key.

An earlier version of this script reimplemented the sweep, and
reimplemented the 24-pair model that `engine.recover(pairs="intra-column")`
already had. Both are gone.

WHAT YOU STILL HAVE TO TELL IT, because nothing on the chip reports it:

  --line-shifts   log2 of the flash read width in BYTES divided by the
                  entry size of the table the LAST ROUND reads. On an
                  8-byte read that is 3 for a byte S-box and 1 for a
                  4-byte word table; on a 16-byte read, 4 and 2. Which of
                  the two a configuration uses is a property of the
                  library, so pass both and let the sweep decide -- the
                  wrong one returns a clean null that reads exactly like
                  a part that does not leak.

  --line-offs     the table's offset within its row. `auto` takes it from
                  the address the DEVICE reported, which is right for a
                  linker-aligned table and wrong for one that landed
                  wherever it landed. Worth 2x to 10x in traces.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import pathlib
import sys

# Be a good citizen: this is the CPU-expensive half and the same box
# drives the bench. Must be set before numpy is imported.
_T = sys.argv[sys.argv.index("--threads") + 1] if "--threads" in sys.argv else "2"
for _v in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS",
           "NUMEXPR_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ.setdefault(_v, _T)

import numpy as np                                            # noqa: E402

_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "flashwhisperer"))

from flashattack import Capture                               # noqa: E402
from flashattack.sweep import (controls_clean, default_geometries,  # noqa: E402
                               ladder)

#: The victim's compiled-in key -- instrumentation/victim/src/ss_main.c.
#: Used only to SCORE a recovery; the device never receives it.
KEY = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")


def load(path: pathlib.Path):
    """One of this repository's captures -> (Capture, meta, is_control)."""
    d = np.load(path, allow_pickle=False)
    meta = json.loads(str(d["meta"]))
    cyc = d["cyc"].astype(np.float64)

    # A build whose on-chip count is bit-exactly constant has nothing to
    # recover from, and so does a tables-in-RAM twin. Both are CONTROLS:
    # scored with the identical call, and if one of them scores, the run
    # is manufacturing and no row of it may be quoted.
    is_control = (len(np.unique(cyc)) == 1) or meta.get("tables_in_ram", False)

    cap = Capture(timing=cyc, mode="ecb-enc", keysize=128,
                  plaintext=d["pt"], ciphertext=d["ct"],
                  label=meta["variant"], meta=meta)
    return cap, meta, is_control


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("npz", nargs="+")
    ap.add_argument("--ladder", default="2500,5000,10000,20000,50000,100000")
    ap.add_argument("--line-shifts", default="3,1")
    ap.add_argument("--line-offs", default="auto")
    ap.add_argument("--models", default="all,intra-column")
    ap.add_argument("--threads", default="2")
    ap.add_argument("--out")
    a = ap.parse_args(argv)

    paths = []
    for pat in a.npz:
        paths.extend(sorted(pathlib.Path(p) for p in glob.glob(pat)))
    if not paths:
        raise SystemExit("no captures matched")

    n_values = [int(x) for x in a.ladder.split(",")]
    shifts = [int(x) for x in a.line_shifts.split(",")]
    models = tuple(m.strip() for m in a.models.split(","))

    ladders, metas, controls, sds = {}, {}, set(), {}
    for p in paths:
        cap, meta, is_control = load(p)
        key = meta["variant"]
        ladders[key], metas[key] = None, meta
        if is_control:
            controls.add(key)

        sd = float(cap.timing.std(ddof=1)) if len(cap.timing) > 1 else 0.0
        sds[key] = sd
        uniq = len(np.unique(cap.timing))
        print("\n== %s%s" % (p.name, "   [CONTROL]" if is_control else ""))
        print("   %s %s [%s]  table 0x%08x  n=%d  sd %.4f  distinct %d"
              % (meta["lib"], meta["lib_version"], meta["cfg_str"],
                 meta["table_addr"], len(cap.timing), sd, uniq))

        # `auto` means the offset the table actually landed at, which the
        # device reported and the capture recorded.
        offs = (None if a.line_offs.strip() == "auto"
                else [int(x, 0) for x in a.line_offs.split(",")])
        geoms = default_geometries(shifts, offs, models)
        if offs is None:
            geoms = [g.__class__(g.line_shift,
                                 meta["table_addr"] & ((1 << g.line_shift) - 1),
                                 g.pairs, g.ct_adjacent) for g in geoms]

        lad = ladder(cap, n_values=n_values, geometries=geoms,
                     verify=(bytes(cap.plaintext[0]), bytes(cap.ciphertext[0])),
                     key_for_truth=KEY, label=key, log=print)
        ladders[key] = lad

    # ---- the summary, and the control gate that decides if it stands ----
    ok, offenders = controls_clean(ladders, controls)

    print("\n== summary (line_shifts=%s, models=%s) ==" % (a.line_shifts, a.models))
    print("   %-40s %-14s %8s  %s" % ("variant", "cfg", "sd", "result"))
    for k in sorted(ladders):
        lad, meta, sd = ladders[k], metas[k], sds[k]
        if not lad.trials:
            note = "constant -- nothing to recover from"
        elif lad.exact_at is not None:
            b = lad.best
            note = "EXACT at n=%d (%s)" % (b.n, b.geometry.label)
        else:
            b = lad.best
            note = "%d/16 best, n=%d (%s)" % (b.bytes_correct, b.n,
                                              b.geometry.label)
        print("   %-40s %-14s %8.4f  %s"
              % (k[:40], meta["cfg_str"], sd,
                 note + ("   [CONTROL]" if k in controls else "")))

    if not ok:
        print("\n   *** THIS RUN IS NOT TRUSTWORTHY ***")
        for name, scored, exact_at in offenders:
            print("   a control scored %d/16%s: %s"
                  % (scored, " and the KEY" if exact_at else "", name))
        print("   A 120-pair vote has five times the freedom of a 24-pair")
        print("   one, and a geometry sweep multiplies that again. If they")
        print("   score on a build with nothing to find, they are")
        print("   manufacturing, and no row above may be quoted.")
    else:
        print("\n   controls clean: %d constant or RAM-twin build(s) scored"
              " nothing under the identical call." % len(controls))

    if a.out:
        rows = []
        for k, lad in ladders.items():
            for t in lad.trials:
                rows.append(dict(variant=k, cfg=metas[k]["cfg_str"],
                                 table=metas[k]["table_addr"], n=t.n,
                                 line_shift=t.geometry.line_shift,
                                 line_off=t.geometry.line_off,
                                 pairs=t.geometry.pairs,
                                 ct_adjacent=t.geometry.ct_adjacent,
                                 bytes_correct=t.bytes_correct,
                                 exact=t.exact, verified=t.verified,
                                 control=k in controls, seconds=t.seconds))
        pathlib.Path(a.out).write_text(json.dumps(rows, indent=2))
        print("   wrote %s" % a.out)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
