"""Talk to a victim directly over its serial port.

    from mcueval import Victim
    with Victim("COM3") as v:
        v.resync()
        print(v.ident())
        print(v.libinfo())
        ct, cyc = v.encrypt_cyc(bytes(16))

This is the ON-CHIP arm: the victim times its own encryption and reports
the count. It's the best case for an attacker and the easiest thing for a
vendor to dismiss, which is why the off-chip instrument exists as well
(see instrumentation/sstimer/). Both speak the same victim protocol, so
the numbers from the two can be compared, since they're measurements of
the same encryptions.

RESYNC IS NOT OPTIONAL, AND THE OBVIOUS VERSION OF IT IS WRONG. The
protocol has no frame marker, so after a reset or a lost byte the two
sides may disagree about where a command starts. resync() sends V_ENC one
byte at a time until a response appears, and whatever that response
answered, the victim is then at a command boundary.

The gotcha, which cost a debugging round in the rig this came from:
polling for a COMPLETE response after each filler byte, with a fixed
sleep, sends one byte too many. At a low baud a seventeen-byte reply
doesn't arrive inside one byte time, so the loop writes again after the
victim has already answered; the victim is then holding a fresh opcode and
eats the next command as its payload. Resync reports success and the very
next command gets no reply at all. STOP WRITING THE MOMENT THE FIRST
INBOUND BYTE APPEARS.
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass

from . import proto as P


class VictimError(RuntimeError):
    pass


@dataclass
class Ident:
    proto_version: int
    flags: int
    layout: str
    table_addr: int
    acr: int
    clk_hz: int

    @property
    def tables_in_ram(self) -> bool:
        return bool(self.flags & P.VI_TABLES_RAM)

    @property
    def reports_cyc(self) -> bool:
        return bool(self.flags & P.VI_REPORTS_CYC)

    @property
    def warmup(self) -> bool:
        return bool(self.flags & P.VI_WARMUP)

    def __str__(self) -> str:
        return ("proto=%d layout=%s table=0x%08x acr=0x%x clk=%d Hz "
                "ram=%d cyc=%d warmup=%d"
                % (self.proto_version, self.layout, self.table_addr, self.acr,
                   self.clk_hz, self.tables_in_ram, self.reports_cyc,
                   self.warmup))


@dataclass
class LibInfo:
    lib: int
    cfg: int
    n_tab: int
    build_byte: int
    tab: tuple
    code: int
    version: int

    @property
    def name(self) -> str:
        return P.LIB_NAMES.get(self.lib, "lib%d" % self.lib)

    @property
    def version_str(self) -> str:
        return "%d.%d.%d" % (self.version >> 16, (self.version >> 8) & 0xFF,
                             self.version & 0xFF)

    def __str__(self) -> str:
        tabs = " ".join("0x%08x" % t for t in self.tab)
        return ("%s %s [%s] tabs=%d (%s) code=0x%08x build=%d"
                % (self.name, self.version_str, P.cfg_str(self.cfg),
                   self.n_tab, tabs, self.code, self.build_byte))


class Victim:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.0):
        import serial  # pyserial; lazy so host-only tests need no dependency
        self.port = port
        self._s = serial.Serial(port, baud, timeout=timeout)
        # Settle before the first byte: a port opened and written to
        # immediately after enumeration drops the first command on this
        # host's CDC driver.
        time.sleep(0.2)
        self._s.reset_input_buffer()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def close(self):
        try:
            self._s.close()
        except Exception:
            pass

    # ---- the one exchange primitive ------------------------------------
    def _xfer(self, op: int, payload: bytes = b"") -> bytes:
        send_len, want = P.EXCHANGE[op]
        if len(payload) != send_len:
            raise ValueError("opcode 0x%02x wants %d payload bytes, got %d"
                             % (op, send_len, len(payload)))
        self._s.write(bytes([op]) + payload)
        head = self._s.read(1)
        if not head:
            raise VictimError("no response to opcode 0x%02x (timeout)" % op)
        if head[0] == P.V_RSP_ERR:
            bad = self._s.read(1)
            raise VictimError("victim rejected opcode 0x%02x (it echoed %r); "
                              "this build may not implement it"
                              % (op, bad))
        if head[0] != (op | P.V_RSP):
            raise VictimError("expected response 0x%02x, got 0x%02x - the "
                              "link is out of step, resync()"
                              % (op | P.V_RSP, head[0]))
        body = self._s.read(want)
        if len(body) != want:
            raise VictimError("short response to 0x%02x: %d of %d bytes"
                              % (op, len(body), want))
        return body

    def resync(self, limit: int = 64) -> int:
        """Return the victim to a known command boundary. Returns how many
        filler bytes it took, which is worth logging: from idle it is 17,
        and a much larger number means something other than a desync."""
        self._s.reset_input_buffer()
        for n in range(1, limit + 1):
            self._s.write(bytes([P.V_ENC]))
            self._s.flush()
            # Wait for the FIRST byte only. Reading a whole response here is
            # the bug described in the module docstring.
            deadline = time.time() + 0.05
            while time.time() < deadline:
                if self._s.in_waiting:
                    time.sleep(0.05)          # let the rest of it land
                    self._s.reset_input_buffer()
                    return n
        raise VictimError("resync failed after %d filler bytes - the victim "
                          "is not answering at all" % limit)

    # ---- the commands ---------------------------------------------------
    def ident(self) -> Ident:
        b = self._xfer(P.V_IDENT)
        ver, flags, layout, _r = b[0], b[1], b[2], b[3]
        tab, acr, clk = struct.unpack("<III", b[4:16])
        return Ident(ver, flags, chr(layout), tab, acr, clk)

    def libinfo(self) -> LibInfo:
        b = self._xfer(P.V_LIBINFO)
        lib, cfg, n_tab, build = b[0], b[1], b[2], b[3]
        t0, t1, t2, code, version = struct.unpack("<IIIII", b[4:24])
        return LibInfo(lib, cfg, n_tab, build, (t0, t1, t2), code, version)

    def encrypt(self, pt: bytes) -> bytes:
        return self._xfer(P.V_ENC, bytes(pt))

    def encrypt_cyc(self, pt: bytes):
        """Returns (ciphertext, cycles). Raises if this build has no counter
        - which is a real answer, not a failure: the uninstrumented arm
        exists on purpose, and reporting zeros would read as a
        constant-time result."""
        b = self._xfer(P.V_ENC_CYC, bytes(pt))
        return b[:16], struct.unpack("<I", b[16:20])[0]

    def set_acr(self, value: int) -> int:
        """Returns the READ-BACK, never the requested value. The board layer
        refuses a configuration its clock cannot run, and this is how the
        caller finds out: the old value comes back."""
        b = self._xfer(P.V_SET_ACR, struct.pack("<I", value))
        return struct.unpack("<I", b)[0]

    def set_trigger(self, on: bool) -> bool:
        b = self._xfer(P.V_TRIG_CTL, bytes([1 if on else 0]))
        return bool(b[0])
