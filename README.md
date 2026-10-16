# flashwhisperer-crypto-eval

*🤖🥩: This document is partially human-written, partially LLM. This repo flags
LLM-written docs as I find they can be more annoying to read as a human bag of 🥩*

What does a microcontroller's flash accelerator setting do to the
execution time of a crypto library's AES? That is the question this
answers, and it only needs a dev board by using on-board measurement
from microcontroller timers.

A part gives you a clock, a console, a trigger pin, an accelerator word
and a cycle counter. A library gives you a key schedule and a one-block
encrypt.

```bash
git submodule update --init          # flashwhisperer, the analysis half
./libs/fetch.sh                      # the pinned library checkouts
make list                            # what parts and libraries there are
make matrix MCU=stm32l152re          # every configuration worth having

python tools/capture.py --mcu stm32l152re --probe <probe-serial> \
       --variant refsbox-sbox-ram0-vcp-b115200-tsystick-w1-pad0 -n 20000
python tools/attack.py results/stm32l152re/*.npz --line-shifts 3,1
```

## Without a board

A good half of this you can run with nothing attached, because the thing
that decides whether there's a channel at all is settled at link time.
You need `arm-none-eabi-gcc` and the pinned checkouts, nothing else.

```bash
./libs/fetch.sh
make list
```
```
parts:
  stm32f429zi    Cortex-M4F, 16 MHz, 4-bit LATENCY + ART I/D caches, DWT or SysTick
  stm32l152re    Cortex-M3, 32 MHz, one-bit LATENCY, no cache - the floor case
libraries:
  mbedtls        Mbed-TLS 3.6.7, axes ROMTAB and FEWER (both default off)
  refsbox        this project's own byte S-box AES - the control, source in-tree
  wolfssl        wolfSSL 5.9.4, axis FEWER; tables are in flash in EVERY build
```

### Watch the control work

Three builds of the same library. Every build prints where its tables
landed, which is the whole residence argument in one place:

```bash
./tools/build.sh MCU=stm32l152re LIB=mbedtls ROMTAB=1 RAMTAB=0   # flash arm
./tools/build.sh MCU=stm32l152re LIB=mbedtls ROMTAB=1 RAMTAB=1   # the RAM twin
./tools/build.sh MCU=stm32l152re LIB=mbedtls ROMTAB=0            # stock
```

The flash arm, tables in `.rodata`:

```
    08000400 R _tabT
    08001400 R _tabS
    08001500 R _tabD
```

The twin, same code and same indices, tables moved by the linker alone:

```
    20000000 R _tabT
    20001000 R _tabS
    20001100 R _tabD
```

And stock Mbed-TLS, which has no tables in flash to begin with and says so
rather than leaving you to notice:

```
    08000400 B _tabS_end
    08000400 B _tabT
    08000400 B _tabT_end
  NOTE: MBEDTLS_AES_ROM_TABLES is OFF - this is STOCK Mbed-TLS.
        Its tables are generated into .bss at run time, so they are
        in RAM and this build is EXPECTED to be quiet. The table
        slots will report address 0. Build ROMTAB=1 for the
        flash-table configuration.
```

Zero-length slots, so the device will report address 0 for all three. That
is a result and not a missing value - it's the negative control that came
with the library, with the positive control one flag away.

### Initial Checks

`check_tables_move.sh` is the cheapest proof that the three linker
fragments are still wired together. A `tab_*.ld` that quietly stopped
matching collects nothing, the slot reports 0, and 0 is a legitimate
answer meaning "this library keeps its tables in RAM" - so a stale
fragment reads as a library with no flash channel:

```bash
make matrix MCU=stm32l152re        # or just the refsbox ram0/ram1 pair
./tools/check_tables_move.sh
```
```
stm32f429zi: S-box flash=0x08001400 ram=0x20001000
stm32l152re: S-box flash=0x08001400 ram=0x20001000
OK: every part's table slot exists and moves.
```

`verify.sh` is the other one: no library source is modified and every
checkout is still on its pin. The claim that nothing upstream was touched
is what makes a result hard to argue with, so it's checked rather than
asserted:

```bash
./tools/verify.sh
```
```
== library checkouts must be pristine at their pinned commits ==
  mbedtls    068ff080b  0 modified/untracked
  wolfssl    3c5eead44  0 modified/untracked

  OK: no library source is modified, and every pin matches.
```

## With a board

One configuration at a time, which is the form to use while bringing a
part up:

```bash
python tools/capture.py --mcu stm32l152re --probe 066DFF565271525067103647 \
       --variant refsbox-sbox-ram0-vcp-b115200-tsystick-w1-pad0 -n 20000
```

Realistically you want the whole matrix, each one built, flashed, read
back off the device and verified before anything is written, and you want
a particular accelerator word rather than whatever the board booted with:

```bash
# every configuration for a part, one flash each
python tools/sweep.py --mcu stm32l152re --probe <serial> -n 20000

# one library's arms only, while you are iterating on it
python tools/sweep.py --mcu stm32l152re --probe <serial> -n 20000 --only wolfssl

# a named accelerator word; the device's READBACK goes in the filename, and
# capture.py aborts if the board refused the setting
python tools/capture.py --mcu stm32f429zi --probe <serial> --acr 0x705 \
       --variant refsbox-sbox-ram0-vcp-b115200-tdwt-w1-pad0 -n 25000
```

The `w1` there is the warm-up, not a wait state. Every axis that changes
the image is in the variant name, because two builds differing only in a
define are not interchangeable and an object file reused across them is a
silent wrong answer rather than a build error.

`tools/longcap.sh` is the same thing for the long runs: it takes the five
L152 library arms to 100 000 (`N=` overrides) and leaves the controls at
20 000, since a bit-exactly constant build buys nothing from more data. It
hardcodes this bench's probe serial and the part, so read it as a record
of what was run rather than something to point at your own board.

Then score it. The quick form sweeps both row widths and both pair models
and tells you which won:

```bash
python tools/attack.py results/stm32l152re/*.npz --line-shifts 3,1
```

Here is a real run of exactly that over the ten L152 configurations,
trimmed to the summary:

```
== summary (line_shifts=3,1, models=120,24) ==
   variant                                  cfg                 sd      n   ls result
   mbedtls-rom0-few0-ram0-vcp-b115200-tsyst stock           0.0000      -    - constant -- control
   mbedtls-rom1-few0-ram0-vcp-b115200-tsyst romtab          0.7828   2500    3 EXACT at n=2500 ls=3, 120-pair
   mbedtls-rom1-few0-ram1-vcp-b115200-tsyst ramtab+romtab   0.0000      -    - constant -- control
   mbedtls-rom1-few1-ram0-vcp-b115200-tsyst romtab+fewer    1.7666   5000    3 EXACT at n=5000 ls=3, 120-pair
   refsbox-sbox-ram0-vcp-b115200-tsystick-w stock           2.1991   5000    3 EXACT at n=5000 ls=3, 24-pair
   refsbox-sbox-ram1-vcp-b115200-tsystick-w ramtab          0.0000      -    - constant -- control
   refsbox-ttable-ram0-vcp-b115200-tsystick stock           0.6717   2500    3 EXACT at n=2500 ls=3, 120-pair
   wolfssl-few0-ram0-vcp-b115200-tsystick-w stock           0.5841   2500    1 6/16 (best, n=2500 ls=1 120-pair)
   wolfssl-few0-ram1-vcp-b115200-tsystick-w ramtab          0.0000      -    - constant -- control
   wolfssl-few1-ram0-vcp-b115200-tsystick-w fewer           2.1918  10000    3 EXACT at n=10000 ls=3, 120-pair

   controls clean: 4 bit-exactly constant or RAM-twin builds scored nothing under the identical call.
```

The last line is the one that makes the rest of the table quotable. A
120-pair vote has five times the freedom of a 24-pair one and a geometry
sweep multiplies that again, so every constant build and every RAM twin is
scored through the identical call. If one of them had scored, no row above
it could be quoted - the run would be manufacturing, and the finding would
be about the attack rather than about the library.

The realistic form pins what you already know and ladders what you don't.
`--line-offs auto` takes the offset from the address the *device* reported,
which beats a guess of zero on a table that didn't land row-aligned:

```bash
python tools/attack.py results/stm32f429zi/*.npz \
       --line-shifts 4,2 --line-offs auto \
       --ladder 25000,50000,100000,250000 --models all,intra-column \
       --threads 4 --out results/stm32f429zi/recoveries.json
```

That offset is in ENTRIES, not in bytes, so `auto` needs the part's row
width as well as the table's address - it reads the width from
`EVAL_LINE_SHIFT` in `eval/mcu/<part>/eval.mk` and prints what it derived.
A 4-byte word table at `0x08001fb8` on a 16-byte row starts at entry
`(0xb8 % 16) // 4 = 2`, not at 0, and an attack handed 0 there reads 0/16
where 2 gives a verified key - the same clean null a part that doesn't leak
gives. Everything captured here is either a byte S-box or a table the
linker put on a 1 KB boundary, where every way of computing the offset
agrees, which is exactly why this was worth writing down rather than
leaving to the reader.

The logs and the `recoveries.json` from both parts are committed under
`results/<part>/`, so the numbers above are checkable without a board even
though the captures themselves are not tracked.

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
written.

The recovery: a statistic is not a channel, and a spread can be large and
yield nothing. `tools/attack.py` runs the last-round recovery and reports
the first trace count at which the exact master key falls out.

## Status

| | |
|---|---|
| parts | six, all measuring: STM32L152RE (M3, no cache), STM32F401RE and STM32F429ZI (M4, ART), STM32G474RE (M4, 8-byte reads), STM32H723ZG (M7, L1 only) and STM32F746ZG (M7, ART *and* L1) |
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

On the H723 the reference cipher gives the exact key at 5 000 traces with
the I-cache on - the configuration production H7 firmware ships - and
only 12/16, flat to 25 000, with both caches off. Turning a cache on made
the part easier to attack and more than twice as fast. Its DTCM twin is
bit-exactly constant over 25 000 traces.
[The full table](results/stm32h723zg/README.md).

On the F746 the answer comes out the other way round, which is the point
of having both. There the D-cache CLOSES the channel - a 256-byte S-box
fits in its 4 KB, so the spread collapses to 0.09 cycles and the recovery
to 0/16 - while the I-cache alone opens it wide enough to give up the
exact key at 5 000 traces. ST's own default for the family, both caches
on, lands at 1/16. Same vendor, same core, same cipher, and the safe
setting on one part is the dangerous one on the other.
[The full table](results/stm32f746zg/README.md).

The two Cortex-M4 parts say the same thing as each other and it is the
cleanest mitigation in the tree. On both, the ART DATA CACHE IS THE
CHANNEL: clearing `DCEN` makes the cipher bit-exactly constant - one
distinct cycle count over 25 000 traces - and makes it faster as well,
because neither cache can hold the table it is being asked to cache.
With `DCEN` set, both fall: the F401 gives up the exact key at 100 000
traces and the G474 at 250 000, both with a compact T-table.
[F401](results/stm32f401re/README.md),
[G474](results/stm32g474re/README.md).

Worth reading those two together for one reason: on both parts the table
that BREAKS produces a SMALLER spread than the table that does not.
Screening cells on sigma would have discarded the only configuration
that yields a key.

Stock Mbed-TLS has no flash-timing channel at all, since
`MBEDTLS_AES_ROM_TABLES` defaults off and its tables get generated into
RAM at run time. A negative control that came with the library, with the
positive control one flag away. wolfSSL has no such default - its tables
are in flash in every build.

### Two reasons a recovery fails, and they look identical

Failure to get a key does not mean it won't work. A couple common problems:

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
ladder. Neither pair model dominates either - on the reference cipher the
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
This allows many boards to be attached to a single host.

## Requirements

`arm-none-eabi-gcc`, Python 3.9+, `numpy`, `pyserial`, `pyocd`. The
firmware builds under WSL on this bench, which is why the line endings are
pinned to LF and the build goes through `tools/build.sh` rather than an
inline `bash -lc`.

flashwhisperer is APACHE2 licensed, but some vendor and HAL code has
other licenses. The license in this repo does not claim any ownership
over other code.