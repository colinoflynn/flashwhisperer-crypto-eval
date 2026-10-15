# Adding a part

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

A port is one C file, one register header, one memory map and one
`board.mk`, and it touches nothing else in the tree. On the first part
done this way that came to about 250 lines of C including startup and the
vector table.

Copy `mcufw/stm32l152re/` and work through
[`../instrumentation/victim/include/ss_board.h`](../instrumentation/victim/include/ss_board.h),
which is the contract. Everything below is the part that isn't obvious
from the header.

## The checklist, in the order that fails fastest

1. `make MCU=<part> LIB=refsbox`. The reference cipher is in-tree, so this
   needs no library fetch and proves the toolchain, the memory map and the
   three linker fragments before anything else can confuse the picture.
2. `make addr` - three slots with plausible addresses. All three reading 0
   means `-fdata-sections` isn't reaching the table-bearing translation
   unit, not that the part has no tables.
3. `RAMTAB=1` and `make addr` again. The slots must move into RAM and
   `_tabload` must stay in flash. If they don't move, the `REGION_ALIAS`
   isn't reaching the sections.
4. Flash it and `SS_V_IDENT`. The table address the device reports must
   equal the one `make addr` printed. If it doesn't, the startup copy loop
   or the vector table is wrong.
5. Capture a few hundred traces. Every ciphertext must match the reference
   AES. This is the check that catches a cipher that is running but being
   driven wrongly, and nothing else in the rig will.
6. The RAM twin must be bit-exactly constant, or close to it. A twin that
   still varies means something other than the table is moving, and every
   number from the flash arm is suspect until it's explained.
7. Run the recovery. A statistic is not a channel.

## The traps

The first three are in the contract header too, at the declaration.
They're repeated here because each one produces *numbers* rather than an
error. The fourth produces no numbers at all, which is why it reads as
something else entirely.

### `ssb_acr_set` must REFUSE what the clock cannot run

Writing an illegal accelerator configuration doesn't fault, it changes how
the very next instruction fetch is served. On the first part here, writing
zero wait states at 32 MHz - where the reference manual makes one
mandatory - stopped the part dead mid-script with four checks already
passed, and from the host it looked like an unrelated crash.

`SS_V_SET_ACR` replies with the readback, never the requested value, so a
caller learns it was refused by getting the old value back. Keep that.

### On a part with caches, "set the accelerator" is a dance

Leaving the accelerator in the state a reset-time write would leave it
means resetting the caches while they are disabled, not just writing an
enable bit, and on some parts waiting for the invalidation to report
complete. Omitting it has invalidated a whole round of results before now.
Do it in the board file; the protocol layer must not know what kind of
part it's talking to.

### The board owns the subtraction

`ssb_cyc_delta(first, second)` exists because a counter may count up
(Cortex-M `DWT->CYCCNT`) or down over 24 bits (SysTick). The first part
ported counted down, and a caller assuming `second - first` would have
produced large positive garbage rather than an error.

Two more ways a cycle counter lies, both silent:

* `DWT->CYCCNT` can stop while `CYCCNTENA` still reads set. Every derived
  interval then comes out exactly zero, and a debugger connection hides
  it. Check that the counter moves at all before trusting a column.
* `DEMCR.TRCENA` is cleared by some debug probes when they disconnect
  after flashing. Arming it once at boot then yields zero for every
  window, which reads exactly like a constant-time result. Re-arm it where
  it's used, not in board init.

### A register that is not there HANGS, and the host blames the UART

Reading a peripheral that is clock-gated, powered down or simply absent
does not fault on every part. On the H723 it just never returns. Nothing
in `ssb_*` has a timeout, so the victim stops mid-reply, and what the
host prints is

```
mcueval.victim.VictimError: short response to 0x03: 8 of 16 bytes
```

which looks like a framing or baud problem and sends you to the UART. The
UART is fine. Count the bytes instead: `SS_V_IDENT`'s payload is version,
flags, layout, reserved, then three `u32`s, so eight bytes means it died
in the *third* word - which is `ssb_acr_get`, and on that part it was one
register inside it.

Bisect it by replacing the body of the suspect hook with a constant and
putting the registers back one at a time. Make sure the define you
bisect with actually reaches the compiler: `build.mk` has no hook for an
extra `-D`, and a first attempt here "proved" all four steps passed
because every build had compiled the same constant. A bisection that does
not change the binary proves nothing. Edit the source, or add the flag to
`COMMON_DEFS` properly.

## `ssb_has_cyc()` returning 0 is a legitimate build

That's the arm with no instrumentation in it. `SS_V_ENC_CYC` then answers
with the error response rather than with zeros, so the host learns it from
the device instead of reading a column of zeros as a result. Don't fake a
counter to make the arm "work".

## What goes in `board.mk`

Five variables and nothing else:

| | |
|---|---|
| `# DESC:` | one line, shown by `make list` |
| `BOARD_CPU` | the `-mcpu`/`-mthumb`/float flags |
| `BOARD_SRC` | the board file and its startup |
| `BOARD_INC` | its include directory |
| `BOARD_LINKS` | which physical links the victim can speak on |
| `BOARD_ACR_WORDS` | accelerator words worth sweeping |

`BOARD_LINKS` is checked: asking for a link a part doesn't offer is a make
error naming the ones it does, rather than a build that comes out wired to
nothing.

And one file beside it: `mcufw/<part>/pyocd.target`, holding the pyocd
target name on one line. `tools/capture.py` reads it so that flashing a
new part needs no argument and no table anywhere else. `python -m pyocd
list` prints the name it resolves for an attached board, and
`python -m pyocd pack install <target>` fetches the support pack when the
listing marks it unsupported - the H723 and F746 both need that, the
L152 and F429 do not.

## When the accelerator is not one register

The contract asks for ONE opaque word, and some parts do not have one.
The H723 is the worked example: its `FLASH_ACR` holds `LATENCY` and
`WRHIGHFREQ` and no accelerator bits at all, every cache on the data path
is the Cortex-M7 L1 over in the core's `SCB`, and they are not even in the
same peripheral. So `mcufw/stm32h723zg/regs.h` packs latency and the two
cache enables into one word, documents the bit positions at the top, and
`ssb_acr_get` reassembles the readback from the three registers.

Two rules if you do that. The packing is the board's, so it goes in the
board's header and nowhere else - the protocol layer and the host must
stay unable to tell a packed word from a raw `FLASH_ACR`. And the
readback must be *read*, never reconstructed from what was requested,
which is what makes a refused configuration visible instead of silently
recorded under the name it was asked for.

Leave a knob out when it cannot reach what you are measuring, and say
why. The H723's ART accelerates the ITCM flash alias, the victim runs
from the AXI alias, so the ART cannot touch the data path - it is not in
the word, and `regs.h` records both that reason and the fact that reading
its register hangs this die anyway.

## Vendor headers

The first part here defines its twenty-odd registers by hand, since
vendoring a CMSIS tree for twenty registers is more to keep in sync than
it saves. Nothing in the contract requires that. A part that genuinely
wants a vendor HAL can bring one - add the includes to `BOARD_INC` and the
sources to `BOARD_SRC`. The rest of the tree doesn't care.

## The flash read width is NOT reported by the device

It's a per-part property that nothing on the chip announces, and the
attack needs it. Scoring an 8-byte row at the default of 4 merges two real
rows into one partition and tests a collision that doesn't exist. Getting
it wrong returns a clean null, not a degraded answer.

Two numbers come out of it, and they are not the same number:

* `EVAL_LINE_SHIFT` in the part's `eval.mk` is log2 of the ROW WIDTH in
  bytes - how many bytes one flash read brings back. It describes the part
  and nothing else, and it belongs in that file because
  `tools/attack.py` reads it: the row width is half of what turns a
  table's address into a row offset, and a number that lives only in a
  dossier is a number no script can reach.
* `--line-shifts` is log2 of the row width divided by the width of ONE
  TABLE ENTRY, so it describes the part *and* the library's table. On an
  8-byte row that's 3 for a byte S-box and 1 for a 4-byte word table.
  Which of them a configuration reads in its last round is not derivable,
  so it's swept.

The two coincide for a byte S-box, which is why they were one number here
for as long as every capture was one. They are also the units the row
offset is in: `--line-offs auto` derives it as
`(table_addr % row_bytes) // entry_bytes`, counted in entries, with the
entry width coming out as `row_bytes >> line_shift`.

Record the row width in the part's dossier when you measure it, and in its
`eval.mk`. Measuring it is a separate exercise: the spacing at which a
computed-call table's timing split disappears is the flash word width,
read straight off the part.

## Adding a library instead

Smaller: a `provider.c` with three functions, three one-line `tab_*.ld`
fragments naming that library's table input sections, a `lib.mk`, and
whatever configuration header the library's own mechanism wants. Add it to
`libs/PINS` if it is fetched, and to the matrix in `../Makefile` and
`../tools/sweep.py`.

Do NOT patch library source. Every axis must be reachable through the
library's own documented configuration mechanism, or through the linker.
The claim that nothing upstream is modified is what makes a result hard to
argue with, and `tools/verify.sh` checks it.
