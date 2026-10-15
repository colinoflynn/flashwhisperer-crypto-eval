"""`--line-offs auto` must count ENTRIES, not bytes. No hardware, no captures.

This test exists because the derivation it checks was wrong for months and
nothing here could tell. `tools/attack.py` used to compute the offset as

    table_addr & ((1 << line_shift) - 1)

which masks the address with a count of entries, so it is the entry offset
only when entries are one byte wide. Two accidents hid it:

  * every library arm with a 4-byte table that this repository has captured
    landed on a 1 KB boundary, where every formula returns 0;
  * everything else here is refsbox, a byte S-box, where the two formulas
    are the same arithmetic.

So the regression needs a table that is neither - a 4-byte entry at an
address that is not row-aligned - which is why the cases below are written
down rather than taken from a capture directory. The worked one is the
F429's wolfSSL Thumb-2 `te`: at (2, 2) those archived captures give a
verified AES-128 master key at 25 000 traces and at (2, 0) they read 0/16
at 25 000 and need 250 000. A wrong offset is not a degraded answer, it is
a clean null, and a clean null is what a part that does not leak looks
like.

WHAT THIS DOES NOT COVER:
  * Whether a part's EVAL_LINE_SHIFT is the right number. It is measured on
    silicon by the research tree's flash mapper and two parts here record
    EVAL_LINE_SHIFT_MEASURED = no; this reads whatever is written.
  * Which table the last round actually reads, or how wide its entries are.
    That is a property of the library configuration and no formula recovers
    it - it is why --line-shifts is swept rather than derived.
  * Scoring. Nothing here runs an attack; the control that the fix moved no
    published number is the recoveries.json check at the bottom, which
    compares offsets rather than recoveries.
"""
from __future__ import annotations

import json
import pathlib
import re
import sys

import pytest

_HERE = pathlib.Path(__file__).resolve().parent
_ROOT = _HERE.parents[1]

sys.path.insert(0, str(_HERE.parent))
from attack import (auto_line_off, entry_bytes_for,           # noqa: E402
                    row_bytes_for)


def _old_formula(table_addr: int, line_shift: int) -> int:
    """The derivation this test is here to keep from coming back."""
    return table_addr & ((1 << line_shift) - 1)


# (name, row_bytes, entry_bytes, addr, expected line_off). Taken from
# tools/geometry.py in the wolfssl-flash-leakage-demo tree, which derives
# this from first principles and whose --check mode agrees with every
# capture that recorded a geometry. The five marked disagree with the old
# masking formula; the rest are there so a "fix" that breaks the agreeing
# cases is caught too.
REFERENCE_CELLS = [
    # 4-byte entries, NOT row-aligned - the cases that matter.
    ("f429_asm", 16, 4, 0x08001FB8, 2),
    ("g474_archived_4t", 8, 4, 0x080030BC, 1),
    ("g474_bare_tl_cl32_off1", 8, 4, 0x0800A004, 1),
    ("g474_vendor_c", 8, 4, 0x08007C14, 1),
    # 4-byte entries on a 1 KB boundary: the accident that kept it quiet.
    ("g474_bare_plain", 8, 4, 0x0800A000, 0),
    ("l152_4table", 8, 4, 0x08000400, 0),
    ("g474_vendor_asm", 8, 4, 0x08006C58, 0),
    # A byte S-box, where the old formula was right all along.
    ("l152_smalltables", 8, 1, 0x08000400, 0),
]

#: Where the table above came from, if it happens to be on this machine.
#: A sibling checkout is not a dependency - the values are written down
#: here so a clone can run this test - but when it IS there the two must
#: not have drifted, and finding that out costs one import.
_SIBLING = (_ROOT.parent / "wolfssl-flash-leakage-demo" / "tools"
            / "geometry.py")


@pytest.mark.parametrize("name,row,entry,addr,want", REFERENCE_CELLS)
def test_matches_the_reference_derivation(name, row, entry, addr, want):
    shift = (row // entry).bit_length() - 1
    assert auto_line_off(addr, row, shift) == want, name


def test_the_four_byte_entry_cases_are_the_ones_that_moved():
    """The point of the fix, stated as a difference rather than a value.

    If this starts passing trivially - no cell where the two formulas
    disagree - the parametrised table above has lost its non-byte-entry
    rows and is back to testing the coincidence.
    """
    moved = []
    for name, row, entry, addr, want in REFERENCE_CELLS:
        shift = (row // entry).bit_length() - 1
        if _old_formula(addr, shift) != want:
            moved.append((name, _old_formula(addr, shift), want))
    assert moved, ("no cell here distinguishes the two formulas, so this "
                   "file is testing the byte-S-box coincidence again")
    # And every one of them has 4-byte entries: a byte S-box cannot move.
    for name, _, _ in moved:
        row, entry = next((r, e) for n, r, e, _a, _w in REFERENCE_CELLS
                          if n == name)
        assert entry != 1, name


@pytest.mark.skipif(not _SIBLING.is_file(),
                    reason="no wolfssl-flash-leakage-demo checkout beside "
                           "this one; REFERENCE_CELLS stands on its own")
def test_reference_cells_have_not_drifted_from_the_sibling_tree():
    """Same question, asked of the implementation that is not this one.

    It derives the geometry per cell from the part's granule and the
    table's entry width, and its own --check mode compares that against
    what each shipped capture recorded. If this disagrees, one of the two
    trees has been edited and the other has not, and the offsets here are
    no longer the ones the captures recover at.
    """
    import importlib.util
    spec = importlib.util.spec_from_file_location("_sib_geometry", _SIBLING)
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)

    matched = 0
    for name, row, entry, addr, want in REFERENCE_CELLS:
        if name not in g.CELLS:                 # renamed or retired there
            continue
        d = g.derive(name)
        assert (d["row_bytes"], d["entry_bytes"], d["addr"], d["line_off"]) \
            == (row, entry, addr, want), name
        matched += 1
    # A test that silently matched nothing is the failure mode this whole
    # file is about, so say how thin it got rather than passing green.
    assert matched >= len(REFERENCE_CELLS) - 1, (
        "only %d of %d cells are still named that in the sibling tree"
        % (matched, len(REFERENCE_CELLS)))


def test_f429_asm_is_the_worked_counter_example():
    """The one with a measured penalty attached, spelled out.

    0x08001fb8 on a 16-byte row read as 4-byte words: 0xb8 % 16 = 8 bytes
    into the row, 8 // 4 = entry 2. The mask gives 0xb8 & 3 = 0.
    """
    assert auto_line_off(0x08001FB8, 16, 2) == 2
    assert _old_formula(0x08001FB8, 2) == 0


def test_byte_sbox_still_agrees_with_the_mask():
    """The control on the change: for one-byte entries nothing may move.

    Most of what this repository has captured is a byte S-box read at the
    part's full row width, where the two formulas are the same arithmetic.
    The arms that are not - wolfSSL at FEWER=0, refsbox's T-table - agree
    for the other reason, their tables having landed on a 1 KB boundary.
    If either half stopped agreeing, a published number moved.
    """
    for row_shift in range(1, 6):               # 2 .. 32-byte rows
        row = 1 << row_shift
        for addr in (0x08000000, 0x08000400, 0x08001401, 0x08001FB8,
                     0x0800A00F, 0x20001000):
            assert (auto_line_off(addr, row, row_shift)
                    == _old_formula(addr, row_shift)), (hex(addr), row)


def test_a_shift_wider_than_the_row_is_a_hypothesis_not_an_error():
    """The F401 sweep ran shifts 2, 3, 4 and 5 against a 16-byte row.

    Only 4 produced a key, and that is how the row width was established -
    so a candidate past the measured row is a hypothesis about the PART,
    and refusing it would take away the sweep that settles the number.
    It is scored at one-byte entries in the row it hypothesises, which is
    the old formula, which is what the committed f401 numbers were scored
    at.
    """
    assert entry_bytes_for(16, 5) is None
    assert auto_line_off(0x08001401, 16, 5) == _old_formula(0x08001401, 5)
    # The boundary itself is a real table: 8-byte row, 8 one-byte entries.
    assert entry_bytes_for(8, 3) == 1
    assert auto_line_off(0x08000404, 8, 3) == 4


# ---------------------------------------------------------------------------
# The row width comes from the part, and the part is on disk
# ---------------------------------------------------------------------------

_PARTS = sorted(p.name for p in (_ROOT / "eval" / "mcu").iterdir()
                if (p / "eval.mk").is_file())


def test_there_are_parts_to_check():
    assert _PARTS, "eval/mcu/*/eval.mk is empty - did the tree move?"


@pytest.mark.parametrize("part", _PARTS)
def test_row_width_is_a_power_of_two_log2_of_what_eval_mk_says(part):
    row = row_bytes_for(part)
    assert row & (row - 1) == 0 and row >= 2
    text = (_ROOT / "eval" / "mcu" / part / "eval.mk").read_text()
    m = re.search(r"^\s*EVAL_LINE_SHIFT\s*[:?]?=\s*(\d+)", text, re.MULTILINE)
    assert m and row == 1 << int(m.group(1))


def test_a_part_with_no_eval_mk_says_so_rather_than_guessing():
    with pytest.raises(SystemExit) as e:
        row_bytes_for("stm32f4xx_imaginary")
    assert "EVAL_LINE_SHIFT" in str(e.value)


# ---------------------------------------------------------------------------
# The control: no committed recovery may move
# ---------------------------------------------------------------------------

def test_no_recorded_recovery_changes_offset():
    """Re-derive every offset in results/*/recoveries.json and compare.

    This is the whole argument that the fix is safe. These files are the
    numbers this repository publishes, they record the table address and
    the geometry each
    trial was scored at, and the part directory gives the row width - so
    the derivation can be checked against them without a board and without
    re-running a ladder. Every table in them is either a byte S-box or a
    1 KB-aligned T-table, which is exactly why the bug stayed invisible.

    It does NOT check the recoveries themselves. Equal offsets at equal
    shifts means the attack was handed identical geometry, which is the
    claim; that the same geometry yields the same bytes is flashattack's
    business and its own tests'.
    """
    files = sorted((_ROOT / "results").glob("*/recoveries*.json"))
    assert files, "no recoveries.json under results/ - this control is empty"
    checked = 0
    for f in files:
        part = f.parent.name
        row = row_bytes_for(part)
        for r in json.loads(f.read_text()):
            if r.get("line_shift") is None:     # never scored: constant, or
                continue                        # shorter than the first rung
            got = auto_line_off(r["table"], row, r["line_shift"])
            assert got == r["line_off"], (
                "%s %s table 0x%08x ls=%d: recorded off=%d, now %d"
                % (f.relative_to(_ROOT), r["variant"], r["table"],
                   r["line_shift"], r["line_off"], got))
            checked += 1
    assert checked > 100, "only %d trials checked - too thin a control" % checked
