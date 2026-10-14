# mcu-crypto-eval

Measure what a microcontroller's **flash accelerator settings** do to the
execution time of a **crypto library's** AES, on a dev board and nothing
else.

One part supplies a clock, a console, a trigger pin, an accelerator word
and a cycle counter. One library supplies a key schedule and a one-block
encrypt. Everything between them is shared and knows about neither.

```bash
git submodule update --init          # flashwhisperer, the analysis half
./libs/fetch.sh                      # the pinned library checkouts
make list                            # what parts and libraries there are
make matrix MCU=stm32l152re          # every configuration worth having

python tools/capture.py --mcu stm32l152re --probe <probe-serial> \
       --variant refsbox-sbox-ram0-vcp-b115200-tsystick-w1-pad0 -n 20000
python tools/attack.py results/stm32l152re/*.npz --line-shift 3
```

## What it measures, and what makes the answer believable

A cipher that reads a table out of flash can take a different number of
cycles depending on **which table entries it reads**, because the flash is
read a whole word at a time and two reads that fall in the same word are
cheaper than two that do not. That is a key-dependent timing channel, and
whether a given library on a given part has one is a question with a yes
or no answer.

Three things here exist to stop that answer being wrong:

**The RAM twin.** Every flash-table configuration has a twin with the
tables relocated into RAM **by the linker** — same code, same instruction
stream, same indices, only the residence differs. Anything that survives
the twin is the instrument, not the flash. On the first part measured here
the flash arm has a spread of 2.2 cycles and the twin is bit-exactly
constant, one distinct value over the whole capture.

**The ciphertext check.** Every captured block is checked against a
reference AES under the victim's compiled-in key, and a capture that fails
is not written. This is not belt and braces: in the work this grew out of,
a library framed correctly, returned success on every call, and produced a
deterministic **wrong** ciphertext because a peripheral clock nothing
documents was off. A framing or length check would have recorded 60 000
traces of a mis-driven cipher as a measurement.

**The recovery.** A statistic is not a channel. A spread can be large and
yield nothing. `tools/attack.py` runs the last-round recovery and reports
the first trace count at which the exact master key falls out.

## Status

| | |
|---|---|
| parts | STM32L152RE (Cortex-M3, no cache) — measured end to end |
| libraries | the in-tree reference cipher, Mbed-TLS 3.6.7, wolfSSL 5.9.4 |
| instruments | on-chip (the victim's own counter) — working |

First hardware result, NUCLEO-L152RE, reference byte-S-box cipher, tables
in flash at `0x08001400`, 32 MHz, one wait state:

| | cycles sd | distinct values | exact master key |
|---|---|---|---|
| tables in flash | 2.1991 | 17 | **5 000 traces** |
| tables in RAM (the twin) | 0.0000 | 1 | never — there is nothing to recover from |

Those numbers reproduce the single-part rig this generalises, which
measured a spread of 2.2033 and the exact key at 5 000 traces on the same
configuration, and the table addresses are identical in both arms. That
match is the check that the restructure changed nothing that matters.

## Layout

| | |
|---|---|
| `instrumentation/` | the wire protocol, the victim loop, the board **contract**, the shared `SECTIONS`, and the host package. Names no part and no library. |
| `libs/<name>` | upstream's own bytes at a pinned commit, as submodules. Nothing here modifies them and `tools/verify.sh` checks that. |
| `mcufw/<part>/` | what makes the part run: clock, console, trigger, accelerator, counter, memory map. |
| `eval/lib/<name>/` | what makes the library run: the provider, its configuration header, and **which input sections hold its tables**. |
| `eval/mcu/<part>/` | what is specific to *measuring* that part. |
| `flashwhisperer/` | the analysis half, as a pinned submodule: the recovery engine, the scoring rule and the row geometry. This repository measures; that one decides. |
| `tools/` | build, capture, sweep, attack, verify. |

[`docs/DESIGN.md`](docs/DESIGN.md) is why the boundaries are where they
are — in particular why the table-residence control is split across three
places, which is the one piece that belongs to neither a part nor a
library. [`docs/PORTING.md`](docs/PORTING.md) is how to add a part, and the
three traps that produce numbers rather than errors.

## Two things worth knowing before you trust a number

**A trace count quoted without the table's address is under-specified.**
The same firmware at three link-time accidents has needed 6 250, 12 500 and
25 000 traces for the same recovery. Every capture records the address the
**device** reported, not the one the directory name implies.

**Resolve a board by its debug probe's serial number, never by a COM
number.** Port numbers move on replug and two boards of one type are
distinguishable by nothing else. The registry this grew out of recorded
`COM3` for the part used to bring this up; the board was on `COM53`, and
the stale value survived that registry's own verifying accessor.

## Requirements

`arm-none-eabi-gcc`, Python 3.9+, `numpy`, `pyserial`, `pyocd`. The
firmware builds under WSL on this bench, which is why the line endings are
pinned to LF and the build is driven through `tools/build.sh` rather than
an inline `bash -lc`.

No licence is declared yet. That is a decision, not an oversight.
