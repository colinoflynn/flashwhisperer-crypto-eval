"""The header and its Python mirror must agree. No hardware, no toolchain.

This test exists because the two drifting apart is SILENT: a wrong opcode
value reads from the host as a dead victim, and a wrong payload length
reads as a desync -- both of which look like a wiring problem and send you
to the bench instead of to the diff.

WHAT THIS DOES NOT COVER:
  * Whether the FIRMWARE agrees with the header. It includes it, so it does
    by construction -- but a provider that writes a response by hand rather
    than through the helpers could still disagree, and nothing here sees
    that. The ciphertext check in tools/capture.py is what catches it.
  * The structure of the IDENT and LIBINFO payloads beyond their lengths.
    Field offsets are asserted in the header's comment and parsed in
    victim.py; they are checked against a live device, not here.
  * The off-chip instrument's own framing, which is a separate protocol
    with its own definition.
"""
from __future__ import annotations

import pathlib
import re

import pytest

_HERE = pathlib.Path(__file__).resolve().parent
_HEADER = _HERE.parents[1] / "protocol" / "victim_proto.h"

import sys
sys.path.insert(0, str(_HERE.parent))
from mcueval import proto as P            # noqa: E402


@pytest.fixture(scope="module")
def defines():
    assert _HEADER.is_file(), f"{_HEADER} missing -- did the tree move?"
    text = _HEADER.read_text()
    out = {}
    for m in re.finditer(r"^#define\s+(SS_\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\s*(?:/\*|$)",
                         text, re.M):
        out[m.group(1)] = int(m.group(2), 0)
    assert out, "parsed no #defines out of the header -- the regex is stale"
    return out


# (header name, python name)
PAIRS = [
    ("SS_PROTO_VERSION", "PROTO_VERSION"),
    ("SS_V_ENC", "V_ENC"),
    ("SS_V_ENC_CYC", "V_ENC_CYC"),
    ("SS_V_IDENT", "V_IDENT"),
    ("SS_V_SET_ACR", "V_SET_ACR"),
    ("SS_V_TRIG_CTL", "V_TRIG_CTL"),
    ("SS_V_LIBINFO", "V_LIBINFO"),
    ("SS_V_RSP", "V_RSP"),
    ("SS_V_RSP_ERR", "V_RSP_ERR"),
    ("SS_V_PT_LEN", "PT_LEN"),
    ("SS_V_CT_LEN", "CT_LEN"),
    ("SS_V_IDENT_LEN", "IDENT_LEN"),
    ("SS_V_LIBINFO_LEN", "LIBINFO_LEN"),
    ("SS_VI_TABLES_RAM", "VI_TABLES_RAM"),
    ("SS_VI_REPORTS_CYC", "VI_REPORTS_CYC"),
    ("SS_VI_HAS_TRIGGER", "VI_HAS_TRIGGER"),
    ("SS_VI_WARMUP", "VI_WARMUP"),
    ("SS_LIB_REFSBOX", "LIB_REFSBOX"),
    ("SS_LIB_MBEDTLS", "LIB_MBEDTLS"),
    ("SS_LIB_WOLFSSL", "LIB_WOLFSSL"),
    ("SS_LIB_STCRYPT", "LIB_STCRYPT"),
    ("SS_LC_RAMTAB", "LC_RAMTAB"),
    ("SS_LC_ROMTAB", "LC_ROMTAB"),
    ("SS_LC_FEWER", "LC_FEWER"),
    ("SS_LC_TIMEOPT", "LC_TIMEOPT"),
]


@pytest.mark.parametrize("c_name,py_name", PAIRS)
def test_value_matches_header(defines, c_name, py_name):
    assert c_name in defines, f"{c_name} is gone from the header"
    assert hasattr(P, py_name), f"proto.py has no {py_name}"
    assert getattr(P, py_name) == defines[c_name], (
        f"{c_name}={defines[c_name]:#x} but proto.{py_name}="
        f"{getattr(P, py_name):#x}")


def test_every_ss_v_opcode_is_mirrored(defines):
    """A new opcode added to the header must reach proto.py. Without this
    the mirror stays 'correct' forever by covering less and less."""
    in_header = {n for n in defines
                 if n.startswith("SS_V_") and not n.endswith("_LEN")
                 and n not in ("SS_V_RSP", "SS_V_RSP_ERR", "SS_V_MAX_RSP")}
    mirrored = {c for c, _ in PAIRS}
    missing = in_header - mirrored
    assert not missing, f"opcodes in the header with no mirror: {sorted(missing)}"


def test_exchange_table_covers_every_opcode():
    opcodes = {P.V_ENC, P.V_ENC_CYC, P.V_IDENT, P.V_SET_ACR, P.V_TRIG_CTL,
               P.V_LIBINFO}
    assert set(P.EXCHANGE) == opcodes


def test_response_opcodes_do_not_collide():
    """Every response is request|0x10, so no request may already have that
    bit, and no two requests may collide once it is set."""
    reqs = list(P.EXCHANGE)
    assert all(not (op & P.V_RSP) for op in reqs)
    rsps = [op | P.V_RSP for op in reqs]
    assert len(set(rsps)) == len(rsps)
    assert P.V_RSP_ERR not in rsps


def test_ident_and_libinfo_lengths_match_their_layout():
    """The documented field layouts are 4 bytes of header plus three and
    five little-endian words respectively."""
    assert P.IDENT_LEN == 4 + 3 * 4
    assert P.LIBINFO_LEN == 4 + 5 * 4
    assert P.EXCHANGE[P.V_IDENT][1] == P.IDENT_LEN
    assert P.EXCHANGE[P.V_LIBINFO][1] == P.LIBINFO_LEN


def test_cfg_str_is_stable():
    assert P.cfg_str(0) == "stock"
    assert P.cfg_str(P.LC_RAMTAB) == "ramtab"
    assert P.cfg_str(P.LC_ROMTAB | P.LC_FEWER) == "romtab+fewer"
