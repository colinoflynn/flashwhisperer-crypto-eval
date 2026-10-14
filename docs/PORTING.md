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

## The three traps

These are in the contract header too, at the declaration. They're
repeated here because each one produces *numbers* rather than an error.

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

## Vendor headers

The first part here defines its twenty-odd registers by hand, since
vendoring a CMSIS tree for twenty registers is more to keep in sync than
it saves. Nothing in the contract requires that. A part that genuinely
wants a vendor HAL can bring one - add the includes to `BOARD_INC` and the
sources to `BOARD_SRC`. The rest of the tree doesn't care.

## The flash read width is NOT reported by the device

It's a per-part property that nothing on the chip announces, and the
attack needs it. `line_shift` is log2 of the flash read width in bytes,
and scoring an 8-byte row at the default of 4 merges two real rows into
one partition and tests a collision that doesn't exist. Getting it wrong
returns a clean null, not a degraded answer.

Record it in the part's dossier when you measure it, and pass it to
`tools/attack.py --line-shifts`. Measuring it is a separate exercise: the
spacing at which a computed-call table's timing split disappears is the
flash word width, read straight off the part.

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
