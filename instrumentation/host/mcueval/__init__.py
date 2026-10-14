"""mcueval - the host side of the victim protocol.

    from mcueval import Victim, proto

The firmware half is instrumentation/victim/; the wire format is
instrumentation/protocol/victim_proto.h and mcueval/proto.py mirrors it.

This package drives a victim DIRECTLY over its serial port, which is the
on-chip arm: the victim times its own encryption. The off-chip instrument
(instrumentation/sstimer/) speaks the same victim protocol through a
second board that latches the edges in hardware, so the two arms measure
the same encryptions and their numbers can be compared rather than argued
about.

No board registry lives here. Which port a given board is on is a property
of a bench, not of this library; see tools/bench.py.
"""
from .victim import Victim, VictimError, Ident, LibInfo
from . import proto

__all__ = ["Victim", "VictimError", "Ident", "LibInfo", "proto"]
__version__ = "0.1.0"
