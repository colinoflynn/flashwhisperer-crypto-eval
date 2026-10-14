# Why the tree is this shape

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

Every boundary here is drawn so that adding one thing edits only that
thing's directory. Adding a part must not touch a library, adding a
library must not touch a part, and neither must touch the instrument.
That's the whole design; the rest of this is the places where it was
hard.

```
instrumentation/   the protocol, the board CONTRACT, the shared SECTIONS
libs/<name>        upstream's own bytes, pinned, never modified
mcufw/<part>/      what makes the part run
eval/lib/<name>/   what makes the library run, and where its tables are
eval/mcu/<part>/   what is specific to MEASURING that part
```

## The two hook sets

A victim image is three pieces that know nothing about each other:

| | owns | lives in |
|---|---|---|
| `ssb_*` | clock, console, trigger, accelerator, cycle counter | `mcufw/<part>/board.c` |
| `prov_*` | key schedule, encrypt one block, what library this is | `eval/lib/<name>/provider.c` |
| the loop | the wire protocol, the measured region, the warm-up | `instrumentation/victim/` |

The contract is in
[`ss_board.h`](../instrumentation/victim/include/ss_board.h) and
[`ss_victim.h`](../instrumentation/victim/include/ss_victim.h). Both
headers carry the traps at the declaration rather than in a separate
document, since a porting mistake here reads as a measurement rather than
as a fault.

The board contract is deliberately the *intersection* of two probes
written independently against the same bench - a library victim and a
synthetic flash-arbitration kernel - which then turned out to want nearly
the same services: a console, a free-running core-rate counter, and one
opaque accelerator word. Anything only one of them needed sits behind an
`ssb_has_*` query that returns 0 by default.

## Table residence, which belongs to neither

Relocating a library's AES tables into RAM, with the code and the
instruction stream and the indices all unchanged, is the control that
separates a flash channel from an instrument artefact. Anything surviving
it is the instrument.

It has to be done in the linker. Most libraries' tables are file-static
and at least one ships inside a prebuilt archive, so none of them can be
moved - or even addressed - from C. But that means the *library's* section
names have to appear inside the *part's* linker script, which makes the
control a cross product of the two and gives it no obvious home.

It's split along the axis each half actually belongs to:

| file | says | belongs to |
|---|---|---|
| `mcufw/<part>/memory.ld` | `MEMORY`, `ENTRY`, `_estack` | the part |
| `instrumentation/victim/ld/link_{flash,ram}.ld` | one `REGION_ALIAS` line | the axis |
| `instrumentation/victim/ld/sections.ld` | the `SECTIONS` block | neither |
| `eval/lib/<name>/tab_{t,s,d}.ld` | which input sections hold the tables | the library |

`sections.ld` `INCLUDE`s the three library fragments at the three points
where the slots are, and `-L` on the link line gives `INCLUDE` its search
path. Adding a library writes three one-line files and edits no shared
file; adding a part writes a memory map and edits no library.

Two constraints that aren't obvious and will bite a port:

* `REGION_ALIAS` must sit between `MEMORY` and the `SECTIONS` that use the
  alias. That's why it's three files and not two - `ld` rejects the other
  order with *memory region `FLASH' for alias `TABS' does not exist*.
* `-fdata-sections` is load-bearing, not a size optimisation. It's what
  gives each table its own input section. A library compiled without it,
  or a prebuilt archive built without it, doesn't get this axis at all,
  and the symptom is every table slot reporting address 0.

## The three table slots, and why 0 is an answer

`SS_V_LIBINFO` reports three addresses: the byte S-box, the word
T-tables, and the decryption tables. A slot of zero length reports
address 0, and that's a result rather than a missing value.

* Stock Mbed-TLS reports all three as 0. Its tables are generated into
  `.bss` at run time, so they never enter `.rodata` and the flash read
  path never sees them.
* wolfSSL's default reports 0 for the S-box slot, because its `Te` carries
  one. So `SS_V_IDENT` reports the first slot that is actually populated
  rather than slot 0 unconditionally.

## Why the device reports its own configuration

The directory name is what someone *meant* to build. `SS_V_LIBINFO` is
what is *running*. Those have disagreed before: a rig default flipped and
`fw.bin` changed meaning without changing name.

So every capture records the device's answer - library, version,
configuration bitmap, all three table addresses, the encrypt entry point,
the accelerator word and the clock - and not the path it was read from.

The table address is in there because a trace count quoted without it is
under-specified. The same firmware at three link-time accidents has
needed 6 250, 12 500 and 25 000 traces for the same recovery.

## Build directory names

Every axis is in the name. Several of them change the link-time layout,
and two builds differing only in a define are not interchangeable images.
One object directory per variant, since an object file reused across two
of them is a silent wrong answer rather than a build error.

## The matrix is written out, not generated

`ROMTAB` exists only for Mbed-TLS, `FEWER` only for the two libraries
that have it, `LAYOUT` only for the in-tree reference cipher. A generated
cross product would build meaningless combinations and report them as
results. The list in [`../Makefile`](../Makefile) and in
[`../tools/sweep.py`](../tools/sweep.py) is explicit, which also makes it
reviewable.

## Two instruments

| | needs | the victim contributes |
|---|---|---|
| on-chip | one board | its own cycle counter around its own encryption |
| off-chip | a second board as a hardware timer | two GPIO writes, or nothing |

The on-chip arm is the best case for an attacker, and the easiest thing
for a vendor to dismiss. The off-chip arm takes the victim out of the
measurement entirely. Both speak the same victim protocol, so the two are
measurements of the same encryptions and can be compared rather than
argued about.

A library offering only self-timing would ship the objection along with
the result, which is why `instrumentation/` carries both.

## No ASCII in the protocol

ChipWhisperer's `simpleserial` was the starting point, and got dropped for
one measured reason: its `hex_decode` is a three-way `if`/`else` chain on
each character, so the parse duration is a function of the plaintext. In
the arm that has no trigger and times the UART edges, that term sits
inside the measured interval and correlates with the plaintext in roughly
the shape a first-round attack looks for. It would manufacture a channel.

## What is deliberately NOT here

* No board registry. Which port a board is on is a property of a bench,
  not of this repository. Tools resolve a port from the debug probe's
  serial number, which is the only stable handle a board has - COM numbers
  move on replug, and two boards of one type are distinguishable by
  nothing else. Not theoretical, either: the registry this grew out of
  recorded `COM3` for the first part brought up here, the board was on
  `COM53`, and the stale value survived that registry's own verifying
  accessor.
* No demos. Attacks on real protocol stacks are a separate concern and a
  separate repository.
* No vendored library source. `libs/` holds submodules at pinned commits
  and nothing in this repository modifies a byte of them;
  [`../tools/verify.sh`](../tools/verify.sh) checks that rather than
  asserting it.
* No analysis. The recovery engine, the scoring rule and the row geometry
  are [`flashwhisperer`](../flashwhisperer), carried as a pinned
  submodule. This repository measures, that one decides.

## What this design does not settle

* Whether the board contract is wide enough. It was drawn from two probes
  on Cortex-M parts. `ssb_cyc_delta` exists because the first part counted
  DOWN in 24 bits, which is mild evidence the seam is in the right place,
  but a part whose counter isn't at core rate, or whose accelerator isn't
  expressible as one word, would stretch it.
* Non-Cortex-M parts. Nothing in the protocol or the host assumes ARM, and
  the counter hook is already wide enough for a RISC-V `mcycle` or an
  MSP430 timer. None has been ported, so that's an argument rather than a
  measurement.
* Whether `eval/mcu/<part>/` earns its own directory. It currently holds
  almost nothing. If it stays that way it should collapse into
  `mcufw/<part>/eval.mk`.
