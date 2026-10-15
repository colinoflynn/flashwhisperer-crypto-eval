"""Recover the key from captures taken by this bench. The part that decides.

    python tools/attack.py results/stm32l152re/*.npz --line-shifts 3,1
    python tools/attack.py results/stm32f429zi/*.npz --line-shifts 4,2

A statistic is not a channel. tools/capture.py reports a spread, and a
spread can be large and yield nothing. This reports whether the exact
master key falls out, and at how many traces.

It's an adapter and nothing more. The sweep over trace counts, pair models
and table geometry, and the control gate deciding whether the run may be
quoted, are all `flashattack.sweep`, since none of that is a property of
this bench. What stays here is only what is: how to read this repository's
.npz metadata, which captures are controls, and the victim's compiled-in
key.

You still have to tell it two things, because nothing on the chip reports
them:

  --line-shifts   log2 of the flash read width in BYTES divided by the
                  entry size of the table the LAST ROUND reads. On an
                  8-byte read that's 3 for a byte S-box and 1 for a 4-byte
                  word table; on a 16-byte read, 4 and 2. Which of the two
                  a configuration uses is a property of the library, so
                  pass both and let the sweep decide. The wrong one
                  returns a clean null that reads exactly like a part that
                  doesn't leak.

  --line-offs     the table's offset within its flash row, COUNTED IN
                  ENTRIES rather than in bytes. `auto` derives it from the
                  address the DEVICE reported and the row width in
                  eval/mcu/<part>/eval.mk:

                      (table_addr % row_bytes) // entry_bytes

                  There are two independent ways to get this wrong and
                  both of them return a clean null rather than a degraded
                  answer, so neither announces itself.

                  ALIGNMENT is the first. A hand-passed 0 is right for a
                  linker-aligned table and wrong for one that landed
                  wherever it landed; `auto` exists to take that off you.
                  Worth 2x to 10x in traces.

                  ENTRY WIDTH is the second, and it is the one that read
                  as a detail until it cost a campaign. The offset counts
                  ENTRIES, so the entry size has to come into it: the
                  F429's wolfSSL Thumb-2 `te`, a 4-byte word table at
                  0x08001fb8 on a 16-byte row, sits at entry
                  (0xb8 % 16) // 4 = 2. This file used to mask the address
                  with (1 << line_shift) - 1, a count of ENTRIES, which
                  gives 0 there - and at (2, 0) that capture reads 0/16 at
                  25 000 traces and needs 250 000, where (2, 2) returns a
                  verified master key at 25 000. The two formulas agree
                  whenever entries are one byte wide, which is most of
                  what this repository has captured, and they also agree
                  on any table the linker put on a 1 KB boundary, which is
                  all the rest of it - so no number here ever moved, and
                  the bug kept quiet until a T-table landed unaligned.
"""
from __future__ import annotations

import argparse
import dataclasses
import glob
import json
import os
import pathlib
import re
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

#: The victim's compiled-in key - instrumentation/victim/src/ss_main.c.
#: Used only to SCORE a recovery; the device never receives it.
KEY = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")


def load(path: pathlib.Path):
    """One of this repository's captures -> (Capture, meta, is_control)."""
    d = np.load(path, allow_pickle=False)
    meta = json.loads(str(d["meta"]))
    cyc = d["cyc"].astype(np.float64)

    # A build whose on-chip count is bit-exactly constant has nothing to
    # recover from, and nor does a tables-in-RAM twin. Both are CONTROLS:
    # scored with the identical call, and if one of them scores, the run
    # is manufacturing and no row of it may be quoted.
    is_control = (len(np.unique(cyc)) == 1) or meta.get("tables_in_ram", False)

    cap = Capture(timing=cyc, mode="ecb-enc", keysize=128,
                  plaintext=d["pt"], ciphertext=d["ct"],
                  label=meta["variant"], meta=meta)
    return cap, meta, is_control


def row_bytes_for(mcu: str) -> int:
    """The part's flash fill granule, in BYTES, from its own eval.mk.

    EVAL_LINE_SHIFT there is log2 of the row width in bytes - the number
    of bytes one flash read brings back, measured on silicon and in no
    datasheet. It is read from the part's directory rather than passed on
    the command line because it is a property of the part, and because the
    F429 spent a campaign with that number living in prose in a README
    where no script could reach it.

    NOT the same thing as --line-shifts, which is log2(row_bytes /
    entry_bytes) and therefore depends on the TABLE as well as the part.
    The two coincide for a byte S-box, which is the whole reason this
    function did not exist until now.

    WHAT THIS DOES NOT CHECK: whether the number is right. eval.mk records
    EVAL_LINE_SHIFT_MEASURED for that, and two parts here say `no`; this
    reads the number either way. Nor does it look at whether the table is
    in flash at all - a tables-in-RAM twin gets a row width it has no use
    for, which is harmless because it is a control and is supposed to
    score nothing.
    """
    if not mcu:
        raise SystemExit(
            "this capture's metadata names no part, so its flash row width\n"
            "cannot be looked up. Pass --line-offs explicitly.")
    mk = _ROOT / "eval" / "mcu" / mcu / "eval.mk"
    if not mk.is_file():
        raise SystemExit(
            "no %s, so the flash row width for this capture is unknown.\n"
            "The row width is what turns a table ADDRESS into a row offset,\n"
            "and nothing on the chip reports it. Either add EVAL_LINE_SHIFT\n"
            "to that file - log2 of the row width in bytes - or pass\n"
            "--line-offs explicitly and take the offset on yourself."
            % mk.relative_to(_ROOT))
    m = re.search(r"^\s*EVAL_LINE_SHIFT\s*[:?]?=\s*(\d+)", mk.read_text(),
                  re.MULTILINE)
    if not m:
        raise SystemExit(
            "%s has no EVAL_LINE_SHIFT. That is log2 of the flash row width\n"
            "in BYTES, measured on silicon, and --line-offs auto cannot\n"
            "derive an entry offset without it. Add it, or pass --line-offs\n"
            "explicitly." % mk.relative_to(_ROOT))
    return 1 << int(m.group(1))


def entry_bytes_for(row_bytes: int, line_shift: int) -> int | None:
    """How wide one table entry is under this candidate, or None.

    `line_shift` is log2(row_bytes / entry_bytes), so against a measured
    row width it pins the entry width: 1 << line_shift entries share a
    row, and the row is row_bytes long.

    None means the candidate asks for MORE entries per row than the row
    has bytes, so the entries would be narrower than a byte. That is not a
    table, but it is not a mistake either - it is a hypothesis that the
    row is wider than eval.mk says, and the sweep is entitled to try it:
    the F401's row width of 16 bytes was established by sweeping 2, 3, 4
    and 5 and watching only 4 produce a key. See `auto_line_off` for what
    the offset means there.
    """
    entry_bytes = row_bytes >> line_shift
    return entry_bytes if entry_bytes >= 1 else None


def auto_line_off(table_addr: int, row_bytes: int, line_shift: int) -> int:
    """Where the table's first entry sits in its row, COUNTED IN ENTRIES.

        entry_bytes = row_bytes >> line_shift
        line_off    = (table_addr % row_bytes) // entry_bytes

    Two numbers go in because two are needed: the row width says which
    bytes share a read, and the entry width says how many entries that
    is. `line_shift` already carries their ratio, so the entry width falls
    out of it - but the ratio alone cannot say where in the row the table
    starts, which is what this returns. Masking the address with
    (1 << line_shift) - 1 answers it only for one-byte entries; see
    --line-offs in the module docstring for the F429 capture that proves
    the difference costs 10x in traces.

    A candidate wider than the measured row - `entry_bytes_for` returning
    None - has only one reading left that is about a real table: one-byte
    entries in a row of (1 << line_shift) bytes, which is the candidate
    disagreeing with eval.mk about the part rather than about the table.
    It is taken at its word, and the offset is the address in that
    hypothesised row. Which is also the old masking formula, so a sweep
    that reaches past the row width scores exactly what it always did.
    """
    entry_bytes = entry_bytes_for(row_bytes, line_shift)
    if entry_bytes is None:
        return table_addr % (1 << line_shift)
    return (table_addr % row_bytes) // entry_bytes


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

    ladders, metas, controls, sds, ns = {}, {}, set(), {}, {}
    for p in paths:
        cap, meta, is_control = load(p)
        # KEY ON THE FILE, NOT ON meta["variant"]. Two captures of one
        # build at different accelerator words carry the SAME variant
        # string - capture.py puts the word in the filename and leaves the
        # metadata's variant alone - so keying on it silently dropped one
        # of them here. Measured the annoying way: a run over three
        # captures reported two, having spent the full ladder on the arm
        # it then discarded. The filename is unique by construction, which
        # is the property this needs.
        key = p.stem
        ladders[key], metas[key] = None, meta
        if is_control:
            controls.add(key)

        sd = float(cap.timing.std(ddof=1)) if len(cap.timing) > 1 else 0.0
        sds[key] = sd
        ns[key] = len(cap.timing)
        uniq = len(np.unique(cap.timing))
        print("\n== %s%s" % (p.name, "   [CONTROL]" if is_control else ""))
        print("   %s %s [%s]  table 0x%08x  n=%d  sd %.4f  distinct %d"
              % (meta["lib"], meta["lib_version"], meta["cfg_str"],
                 meta["table_addr"], len(cap.timing), sd, uniq))

        # `auto` means the offset the table actually landed at: the
        # address the device reported, read against the row width in the
        # part's eval.mk. It is PER GEOMETRY, because the offset is in
        # entries and each candidate shift implies a different entry
        # width - the same address is entry 0 of a byte row and entry 2 of
        # the same row read as 4-byte words.
        offs = (None if a.line_offs.strip() == "auto"
                else [int(x, 0) for x in a.line_offs.split(",")])
        geoms = default_geometries(shifts, offs, models)
        if offs is None:
            row = row_bytes_for(meta.get("mcu"))
            geoms = [dataclasses.replace(
                         g, line_off=auto_line_off(meta["table_addr"], row,
                                                   g.line_shift))
                     for g in geoms]
            # Printed, not assumed: the offset is the number that decides
            # whether the run means anything, and it is derived from two
            # numbers neither of which is on the chip. `?B` is a candidate
            # shift wider than the measured row - a hypothesis about the
            # PART, scored at one-byte entries.
            print("   row %d bytes (eval/mcu/%s/eval.mk)  auto off: %s"
                  % (row, meta["mcu"],
                     "  ".join(
                         "ls=%d/%s->%d"
                         % (s,
                            ("%dB" % entry_bytes_for(row, s)
                             if entry_bytes_for(row, s) else "?B"),
                            auto_line_off(meta["table_addr"], row, s))
                         for s in shifts)))

        lad = ladder(cap, n_values=n_values, geometries=geoms,
                     verify=(bytes(cap.plaintext[0]), bytes(cap.ciphertext[0])),
                     key_for_truth=KEY, label=key, log=print)
        ladders[key] = lad

    # ---- the summary, and the control gate that decides if it stands ----
    ok, offenders = controls_clean(ladders, controls)

    print("\n== summary (line_shifts=%s, models=%s) ==" % (a.line_shifts, a.models))
    # The accelerator word is in here because on some parts it IS the
    # axis, and two rows that differ only in it otherwise read as a
    # duplicate.
    print("   %-40s %-14s %7s %8s  %s"
          % ("capture", "cfg", "acr", "sd", "result"))
    for k in sorted(ladders):
        lad, meta, sd = ladders[k], metas[k], sds[k]
        if not lad.trials:
            # An empty ladder has TWO causes and they are not the same
            # answer. A bit-exactly constant capture really has nothing in
            # it. A capture SHORTER than the smallest rung was simply never
            # scored, and calling that "constant" reports a measurement
            # nobody took - which it did, on a 2 000-trace F746 pilot left
            # in a directory of 25 000-trace cells.
            if ns[k] < min(n_values):
                note = ("only %d traces, below the smallest ladder rung (%d)"
                        " - NOT SCORED" % (ns[k], min(n_values)))
            else:
                note = "constant - nothing to recover from"
        elif lad.exact_at is not None:
            b = lad.best
            note = "EXACT at n=%d (%s)" % (b.n, b.geometry.label)
        else:
            b = lad.best
            note = "%d/16 best, n=%d (%s)" % (b.bytes_correct, b.n,
                                              b.geometry.label)
        print("   %-40s %-14s %7s %8.4f  %s"
              % (k[:40], meta["cfg_str"], "0x%x" % meta.get("acr", 0), sd,
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
