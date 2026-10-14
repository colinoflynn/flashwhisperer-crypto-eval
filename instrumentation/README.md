# instrumentation

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

The bit that knows about neither a part nor a library.

| | |
|---|---|
| `protocol/victim_proto.h` | the wire format, and the one definition of it |
| `victim/include/ss_board.h` | the board contract - everything a new part supplies |
| `victim/include/ss_victim.h` | the provider contract - everything a new library supplies |
| `victim/src/` | the protocol loop and the measured region |
| `victim/ld/` | the shared `SECTIONS` block and the two residence variants |
| `host/mcueval/` | the host side of the same protocol |
| `host/tests/` | the header and its Python mirror must agree |
| `sstimer/` | the off-chip instrument - NOT yet moved over |

## The measured region

One function, in `victim/src/ss_victim.c`, and everything about it is
deliberate:

```c
ssb_irq_off();
prov_encrypt(ZERO, warm);        /* the warm-up */
if (g_trig_on) ssb_trig_high();
t0 = ssb_cyc_read();
prov_encrypt(in, out);           /* <- the only thing being measured */
t1 = ssb_cyc_read();
if (g_trig_on) ssb_trig_low();
ssb_irq_on();
```

Interrupts are masked even on a build that enables none. The cost is two
instructions outside the trigger window, and it removes the question
entirely, which is cheaper than answering it in a write-up.

The warm-up runs a FIXED plaintext first, so trace *k* doesn't depend on
trace *k−1*'s plaintext through the table-line state. It sits outside the
trigger window and inside the UART interval, which is most of why an
externally timed turnaround runs about twice the trigger pulse width.

Nothing else is allowed in between. The board layer owns everything
before it, and the provider owns exactly "encrypt one block".

## Two instruments, and why both

| | needs | the victim contributes |
|---|---|---|
| on-chip | one board | its own cycle counter |
| off-chip | a second board as a hardware timer | two GPIO writes, or nothing |

The on-chip arm is the best case for an attacker, and the easiest thing
for a vendor to dismiss ("you instrumented it yourself"). The off-chip arm
takes the victim out of the measurement: a driver board sends the
plaintext over a plain UART and latches the edges in hardware, so the
measured interval contains no victim measurement code at all.

Both speak the same victim protocol, so the two are accounts of the same
encryptions and can be compared rather than argued about. Where this was
done before, the same encryptions gave the exact key from all three
accounts - the victim's own counter, the trigger pulse width, and the UART
turnaround with no victim instrumentation whatsoever - at rising trace
counts, but all three.

`sstimer/` is empty. The off-chip driver firmware and its host library
haven't been moved into this repository yet. Until they are, only the
on-chip arm exists here, and a result from it carries the objection the
off-chip arm is there to answer.

## No ASCII on the link

ChipWhisperer's `simpleserial` was the starting point, and got dropped for
one measured reason: its `hex_decode` is a three-way `if`/`else` chain on
each character, so the parse duration is a function of the plaintext. In
the arm that times the UART edges, that term sits inside the measured
interval and correlates with the plaintext in roughly the shape a
first-round attack looks for. It would manufacture a channel.

So the link is raw binary, and every opcode's payload length is derivable
from the opcode alone, which is what makes a byte-at-a-time resync sound -
there's no state the other side has to guess.

## Resync, and the wrong way to write it

There's no frame marker, so after a reset or a lost byte the two sides may
disagree about where a command starts. Send `SS_V_ENC` one byte at a time
until a response appears; whatever that response answered, the victim is
then at a command boundary. The filler must be a *valid* opcode so that
wherever it lands it starts a command the victim will finish.

Note that the obvious version is wrong, and it cost a debugging round.
Polling for a *complete* response after each filler byte, with a fixed
sleep, sends one byte too many: at a low baud a seventeen-byte reply
doesn't arrive inside one byte time, so the loop writes again after the
victim has already answered. The victim is then holding a fresh opcode and
eats the next command as its payload, so resync reports success and the
very next command gets no reply at all. Stop writing the moment the first
inbound byte appears.

## What the host does not do

There's no board registry here. Which port a board is on is a property of
a bench, not of this library; `tools/capture.py` resolves a port from the
debug probe's serial number, which is the only stable handle a board has.
