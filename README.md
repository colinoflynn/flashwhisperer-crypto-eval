# mcu-crypto-eval

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

What does a microcontroller's flash accelerator setting do to the
execution time of a crypto library's AES? That is the question this
answers, and it only needs a dev board - no scope, no probe.

A part gives you a clock, a console, a trigger pin, an accelerator word
and a cycle counter. A library gives you a key schedule and a one-block
encrypt. Everything in between is shared, and knows about neither.

```bash
git submodule update --init          # flashwhisperer, the analysis half
./libs/fetch.sh                      # the pinned library checkouts
make list                            # what parts and libraries there are
make matrix MCU=stm32l152re          # every configuration worth having

python tools/capture.py --mcu stm32l152re --probe <probe-serial> \
       --variant refsbox-sbox-ram0-vcp-b115200-tsystick-w1-pad0 -n 20000
python tools/attack.py results/stm32l152re/*.npz --line-shift 3
```

## What it measures

A cipher that reads a table out of flash can take a different number of
cycles depending on which entries it reads, because the flash is read a
whole word at a time and two reads that fall in the same word are cheaper
than two that don't. That's a key-dependent timing channel, and whether a
given library on a given part has one is a question with a yes or no
answer.

Three things here exist to stop that answer being wrong.

The RAM twin: every flash-table configuration has a twin with the tables
relocated into RAM by the linker. Same code, same instruction stream, same
indices, only the residence differs. So anything that survives the twin is
the instrument rather than the flash. On the first part measured here the
flash arm has a spread of 2.2 cycles and the twin is bit-exactly constant,
one distinct value over the whole capture.

The ciphertext check: every captured block is checked against a reference
AES under the victim's compiled-in key, and a capture that fails is never
written. This isn't belt and braces. In the work this grew out of, a
library framed correctly, returned success on every call, and produced a
deterministic WRONG ciphertext, because a peripheral clock that nothing
documents was off. A framing or length check would have filed 60 000
traces of a mis-driven cipher as a measurement.

The recovery: a statistic is not a channel, and a spread can be large and
yield nothing. `tools/attack.py` runs the last-round recovery and reports
the first trace count at which the exact master key falls out.

## Status

| | |
|---|---|
| parts | STM32L152RE (M3, no cache) measured across the matrix; STM32F429ZI (M4F, ART) ported and measuring |
| libraries | the in-tree reference cipher, Mbed-TLS 3.6.7, wolfSSL 5.9.4 |
| instruments | on-chip (the victim's own counter). The off-chip instrument is NOT moved over yet. |

Ten configurations on the L152, every ciphertext checked -
[the full table](results/stm32l152re/README.md). "Exact key" means the
AES-128 master key, from inverting the recovered round-10 key:

| | table | cycles sd | exact key at |
|---|---|---|---|
| Mbed-TLS `ROM_TABLES` | `0x08001400` | 0.7828 | 2 500 traces |
| reference cipher, four T-tables | `0x08001400` | 0.6717 | 2 500 |
| reference cipher, byte S-box | `0x08001400` | 2.1991 | 5 000 |
| Mbed-TLS `ROM_TABLES` + `FEWER` | `0x08000800` | 1.7666 | 5 000 |
| wolfSSL `SMALL_TABLES` | `0x08000400` | 2.1918 | 10 000 |
| wolfSSL, stock | `0x08000400` | 0.5841 | never - 6/16 flat to 100 000 |
| stock Mbed-TLS | none in flash | 0.0000 | no channel at all |
| every RAM twin | RAM | 0.0000 | control, all clean |

On the F429 the reference cipher gives the exact key at 100 000 traces
(2/16 at 25 k, 10/16 at 50 k), with its RAM twin at 0/16.

Stock Mbed-TLS has no flash-timing channel at all, since
`MBEDTLS_AES_ROM_TABLES` defaults off and its tables get generated into
RAM at run time. A negative control that came with the library, with the
positive control one flag away. wolfSSL has no such default - its tables
are in flash in every build.

### Two reasons a recovery fails, and they look identical

Both of these bit me here, and I misread both the first time.

* The pair model. Scoring only the 24 intra-column byte pairs, neither
  third-party library recovered anything at 20 000 traces. About half of
  their consecutive round-10 reads cross a column boundary, and that model
  can't express those pairs. All 120 pairs, on the same capture files,
  reach the exact key at 2 500. More traces would never have helped, and
  the margin scaling says so without taking any: a real margin is in
  cycles and holds its size as n grows, a noise margin falls as 1/√n.
* The trace count. On the F429 the model was fine and the data was short,
  20 000 gave 3/16 and 100 000 gives the key.

So a partial recovery that doesn't improve with data is a model problem,
and one that climbs is a data problem. Telling them apart costs one
ladder; guessing instead costs a wrong conclusion in a README (ask me how
I know). Neither pair model dominates either - on the reference cipher the
narrow one wins - so `tools/attack.py` runs both and says which.

Every table address, the spread, each recovery trace count and the one
partial result (6/16) match the single-part rig this generalises.

## Layout

| | |
|---|---|
| `instrumentation/` | the wire protocol, the victim loop, the board contract, the shared `SECTIONS`, and the host package. Names no part and no library. |
| `libs/<name>` | upstream's own bytes at a pinned commit, as submodules. Nothing here modifies them and `tools/verify.sh` checks that. |
| `mcufw/<part>/` | what makes the part run: clock, console, trigger, accelerator, counter, memory map. |
| `eval/lib/<name>/` | what makes the library run: the provider, its configuration header, and which input sections hold its tables. |
| `eval/mcu/<part>/` | what is specific to *measuring* that part. |
| `flashwhisperer/` | the analysis half, as a pinned submodule: the recovery engine, the scoring rule and the row geometry. This repo measures, that one decides. |
| `tools/` | build, capture, sweep, attack, verify. |

[`docs/DESIGN.md`](docs/DESIGN.md) is why the boundaries sit where they
do, in particular why the table-residence control is split across three
places, which is the one piece belonging to neither a part nor a library.
[`docs/PORTING.md`](docs/PORTING.md) is how to add a part, and the three
traps that produce numbers rather than errors.

## Gotchas

A trace count quoted without the table's address is under-specified. The
same firmware at three link-time accidents has needed 6 250, 12 500 and
25 000 traces for the same recovery. Every capture records the address the
*device* reported, not the one the directory name implies.

Resolve a board by its debug probe's serial number, never by a COM number.
Port numbers move on replug and two boards of one type are
distinguishable by nothing else. The registry this grew out of recorded
`COM3` for the part used to bring this up; the board was on `COM53`, and
the stale value survived that registry's own verifying accessor.

## Requirements

`arm-none-eabi-gcc`, Python 3.9+, `numpy`, `pyserial`, `pyocd`. The
firmware builds under WSL on this bench, which is why the line endings are
pinned to LF and the build goes through `tools/build.sh` rather than an
inline `bash -lc`.

No licence is declared yet. That is a decision, not an oversight.
