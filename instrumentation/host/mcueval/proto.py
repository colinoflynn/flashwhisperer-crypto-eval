"""Mirror of instrumentation/protocol/victim_proto.h.

The header is the one definition; this is the host's copy of it, and
tests/test_proto.py parses the header and checks the two agree - no
hardware, no toolchain. That test exists because the two drifting apart is
silent: a wrong opcode value reads as a dead victim, and a wrong payload
length reads as a desync.
"""
from __future__ import annotations

PROTO_VERSION = 1

# requests; a response opcode is the request with RSP set
V_ENC = 0x01          # pt[16]  -> 0x11 ct[16]
V_ENC_CYC = 0x02      # pt[16]  -> 0x12 ct[16] cyc u32
V_IDENT = 0x03        # --      -> 0x13 ident[16]
V_SET_ACR = 0x04      # u32     -> 0x14 u32 read-back
V_TRIG_CTL = 0x05     # u8      -> 0x15 u8
V_LIBINFO = 0x06      # --      -> 0x16 libinfo[24]

V_RSP = 0x10
V_RSP_ERR = 0x1F

PT_LEN = 16
CT_LEN = 16
IDENT_LEN = 16
LIBINFO_LEN = 24

#: request opcode -> (payload length sent, payload length expected back)
EXCHANGE = {
    V_ENC:      (PT_LEN, CT_LEN),
    V_ENC_CYC:  (PT_LEN, CT_LEN + 4),
    V_IDENT:    (0, IDENT_LEN),
    V_SET_ACR:  (4, 4),
    V_TRIG_CTL: (1, 1),
    V_LIBINFO:  (0, LIBINFO_LEN),
}

# SS_VI_* - the IDENT flags
VI_TABLES_RAM = 0x01
VI_REPORTS_CYC = 0x02
VI_HAS_TRIGGER = 0x04
VI_WARMUP = 0x08

# SS_LIB_*
LIB_REFSBOX = 1
LIB_MBEDTLS = 2
LIB_WOLFSSL = 3
LIB_STCRYPT = 4

LIB_NAMES = {
    LIB_REFSBOX: "refsbox",
    LIB_MBEDTLS: "mbedtls",
    LIB_WOLFSSL: "wolfssl",
    LIB_STCRYPT: "stcrypt",
}

# SS_LC_* - the configuration bitmap
LC_RAMTAB = 0x01
LC_ROMTAB = 0x02
LC_FEWER = 0x04
LC_TIMEOPT = 0x08

LC_NAMES = [
    (LC_RAMTAB, "ramtab"),
    (LC_ROMTAB, "romtab"),
    (LC_FEWER, "fewer"),
    (LC_TIMEOPT, "timeopt"),
]


def cfg_str(cfg: int) -> str:
    """Render a configuration bitmap the way a filename would."""
    on = [name for bit, name in LC_NAMES if cfg & bit]
    return "+".join(on) if on else "stock"
