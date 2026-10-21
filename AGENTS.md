# Working on flashwhisperer-crypto-eval

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited
for that real-life touch and typos.*

Notes for an LLM agent using this repository as a framework: you have a firmware image or
a crypto library and you want to know whether its AES has a flash-timing channel on a
given part. [`README.md`](README.md) covers what the harness does and how to run it,
[`docs/DESIGN.md`](docs/DESIGN.md) why the boundaries sit where they do, and
[`docs/PORTING.md`](docs/PORTING.md) how to add a *part*. This file is the rest: how to
bring a new *target* through, and the traps that each cost real bench time to learn.

Most of what is below is not about this code. It is about the handful of ways a timing
evaluation returns a confident wrong answer, and every one of them has happened.

## Talking to me

Pirate speak in the console, every reply, and address me as the captain. Keep it up when
the news is bad too. It's also how I can tell at a glance whether you read this file.

Documents are plain language. Markdown, docstrings, code comments, commit messages,
anything that lands in the repo gets normal English. A pirate commit message is a bug.

## This repository measures. It does not decide.

The split is load-bearing and it is the first thing to get straight, because almost every
question of the form "why does the attack not do X" is answered by "X is in the other
half".

| | |
|---|---|
| here | build a victim image, flash it, read its configuration back off the device, capture timings, check every ciphertext |
| [`flashwhisperer`](flashwhisperer) | the recovery engine, the scoring rule, the row geometry, the control gate. A pinned submodule |

[`tools/attack.py`](tools/attack.py) is an adapter and nothing more: it knows how to read
this repository's `.npz` metadata, which captures are controls, and the victim's
compiled-in key. The sweep over trace counts, pair models and geometry, and the gate that
decides whether a run may be quoted at all, are
[`flashwhisperer/flashattack/sweep.py`](flashwhisperer/flashattack/sweep.py). If you want
to change how a recovery is scored, you are in the wrong repository, and the pin does not
update itself.

What is deliberately absent, so you don't go looking:

* **No off-chip instrument.** `instrumentation/sstimer/` is an empty directory. Only the
  on-chip arm exists here, where the victim times its own encryption with its own counter.
  That is the best case for an attacker and the easiest thing for a vendor to dismiss
  ("you instrumented it yourself"), so a result from this tree ships with the objection
  the off-chip arm is there to answer. Say so when you write one up.
* **No board registry.** Which port a board is on is a property of a bench, not of this
  repository. Tools resolve a board from its debug probe's serial number.
* **No demos, and no vendored library source.** [`libs/README.md`](libs/README.md) holds submodules
  at pinned commits and nothing here modifies a byte of them.
* **No fixed-plaintext spread.** See *The control this harness does not give you* below.
  This is a gap, not a decision.
* **Nothing arbitrates access to a board.** If several agents share a bench, two of them
  opening the same serial port produce a capture that looks merely noisy rather than
  failing, and you find out hours in. Sort that out above this repository.

Docstrings and comments cite paths like `research/targets/*.md` and `results/<campaign>/`
for their measured numbers. Those are in the private research repo this was extracted
from. They are provenance, not imports; nothing loads them, so don't create directories to
make them resolve.

## Two ways in, and only one of them can produce a measured channel

Decide which you are doing before you start, because the available controls differ and the
claim you can make at the end differs with them.

**Path A — you control the build.** The library's source or its configuration mechanism is
in your hands, so you can add it as a provider and get the whole matrix: the flash arm,
its RAM twin, and the recovery. This is the only path that yields *"this configuration
leaks, and here is the trace count"*.

**Path B — you have a binary and nothing else.** You cannot relink it, so **you cannot
build the RAM twin, and the twin is the control that separates a flash channel from an
instrument artefact.** What you can still do is answer the question the channel rests on —
*are the tables read from flash at run time* — statically, with the analysis half:

```bash
PYTHONPATH=flashwhisperer python -m flashleak fwscan     <image>     # what tables are in it
PYTHONPATH=flashwhisperer python -m flashleak placement  <image> --part <part>
PYTHONPATH=flashwhisperer python -m flashleak codescan   <image>     # no byte signature
```

`fwscan` answers the library question, `placement` the build question. `--part` is not
optional in practice: without it anything in an SRAM region comes back `UNDETERMINED`
deliberately, because on one Cortex-M7 family `0x2000_0000` is TCM and `0x2400_0000` is
cached SRAM, and that boundary moves inside the family. `codescan` looks for the *shape* of
a register-offset load instead of a table's bytes, which is what you need for anything with
no byte signature — post-quantum, or just new.
[`flashwhisperer/docs/STATIC_ANALYSIS.md`](flashwhisperer/docs/STATIC_ANALYSIS.md) is the
long version, including the section on where static analysis necessarily fails.

A Path B result is an **exposure** claim, not a measured channel. Write it as one. The
honest upgrade is to rebuild the same library at the same configuration through Path A and
measure that, saying plainly that it is a reconstruction of the shipped build rather than
the shipped build itself.

## Adding a library, in the order that fails fastest

Smaller than adding a part: a `provider.c` with three functions, three one-line `tab_*.ld`
fragments naming that library's table input sections, a `lib.mk`, and whatever
configuration header the library's own mechanism wants.
[`eval/lib/wolfssl/lib.mk`](eval/lib/wolfssl/lib.mk) is the worked example;
[`instrumentation/victim/include/ss_victim.h`](instrumentation/victim/include/ss_victim.h)
is the contract.

1. **`make MCU=<a part that has been measured> LIB=<new>`.** Compose against a part whose
   numbers you already trust, so a failure here is yours and not the port's.
2. **`make addr`.** Three slots with plausible addresses. **All three reading 0 does not
   mean the library has no tables in flash** — it means either `-fdata-sections` isn't
   reaching the table-bearing translation unit, or your `tab_*.ld` fragments don't name the
   sections the compiler actually emitted. Read the real names off the object with
   `arm-none-eabi-objdump -h`; don't guess them from the C.
3. **`RAMTAB=1`, `make addr` again.** The slots must move into RAM and `_tabload` must stay
   in flash. If they don't move, the `REGION_ALIAS` isn't reaching the sections, and both
   arms are measuring the same thing.
4. **Flash it and read `SS_V_LIBINFO`.** The table address the *device* reports must equal
   the one `make addr` printed. The directory name is what someone meant to build;
   `SS_V_LIBINFO` is what is running, and those have disagreed before.
5. **Capture a few hundred traces.** Every ciphertext must match the reference AES under
   the victim's compiled-in key. This is the check that catches a cipher that is running
   but being driven wrongly, and **nothing else in the rig will.** See below for why it
   earns its own section.
6. **The RAM twin must be bit-exactly constant, or close to it.** A twin that still varies
   means something other than the table is moving, and every number from the flash arm is
   suspect until it's explained.
7. **Run the recovery.** A statistic is not a channel.
8. **`./tools/verify.sh`.** No library source modified, every pin matched.

Then wire it into the matrix — and note that this is **two** edits, in
[`Makefile`](Makefile) and in `MATRIX` in [`tools/sweep.py`](tools/sweep.py). Forgetting
the second means `make matrix` builds your library and the sweep silently never captures
it. Add the pin to [`libs/PINS`](libs/PINS) if it is fetched.

### The rules that aren't negotiable

**Do not patch library source.** Every axis must be reachable through the library's own
documented configuration mechanism or through the linker. That claim is what makes a result
hard to argue with, so it is checked rather than asserted —
[`tools/verify.sh`](tools/verify.sh) fails on a modified file or a moved pin. If an axis you
want needs a patch, the axis is out of scope; say so instead of taking it.

**The provider is the shortest glue that works.** Everything in `prov_encrypt` runs inside
the measured window. Nothing in a provider configures its library.

**Write the matrix out; never generate a cross product.** `ROMTAB` exists only for one
library, `FEWER` only for the ones that have it, `LAYOUT` only for the in-tree reference
cipher. A generated product builds meaningless combinations and reports them as results.

**An axis that changes the image must appear in the variant string.** That is what
`LIB_VARIANT` in your `lib.mk` is for. An axis that changes the image without changing the
name means two builds share one object directory *and* one capture name, so the second
overwrites the first — hours after anyone could have noticed, and the capture on disk is
then filed under the configuration it is not. [`build.mk`](build.mk) is explicit about this
at the top and it is the single most expensive clerical error available here.

**Upstream's defaults are not what shipped.** Check where the *given build* puts its
tables. One library in this tree has no ROM/RAM switch at all and its tables are in flash
in every build; another defaults to generating them into RAM at run time and reports
address 0 for all three slots, which is a *result* — a negative control that came with the
library, with the positive control one flag away. Do not reason from a library's
documentation to a firmware's behaviour.

**Check that the axis is still live.** [`libs/fetch.sh`](libs/fetch.sh) warns when the one
upstream file an axis depends on has stopped mentioning the macro that axis sets. An inert
axis produces a null for the wrong reason, and a null is exactly what a working
countermeasure looks like.

## The two numbers nothing reports

Read this one if you read nothing else. Both are inputs the attack cannot derive, both are
per-build rather than per-part, and **guessing either one wrong gives a clean null, not a
degraded result.** A clean null reads exactly like a target that doesn't leak, which is how
an evaluation gets written up backwards.

`--line-shifts` is log2 of (flash read width in bytes / table entry size):

| flash read width | byte S-box | 4-byte T-table |
|---|---|---|
| 8 bytes | 3 | 1 |
| 16 bytes | 4 | 2 |
| 32 bytes | 5 | 3 |

`--line-offs` is where the table starts inside its flash row, **counted in table ENTRIES,
not bytes.** Pure link-order luck. `auto` derives it from the address the *device* reported
and the row width in `eval/mcu/<part>/eval.mk`, as
`(table_addr % row_bytes) // entry_bytes`. Use `auto`; a hand-passed 0 is right for a
linker-aligned table and wrong for one that landed wherever it landed, and that is worth
2x to 10x in traces.

The entry-width half of that formula is the one that read as a detail until it cost a
campaign: a 4-byte word table at `0x08001fb8` on a 16-byte row sits at entry
`(0xb8 % 16) // 4 = 2`. At offset 2 those archived captures give a verified master key at
25 000 traces; at offset 0 they read 0/16 at 25 000 and need 250 000.
[`tools/tests/test_geometry.py`](tools/tests/test_geometry.py) is the regression, and its
docstring is the house style for a new check — it says what it does *not* cover.

Where to get the row width, in order of preference:

1. `EVAL_LINE_SHIFT` in `eval/mcu/<part>/eval.mk`, which
   [`tools/attack.py`](tools/attack.py) reads. **Next to it, check
   `EVAL_LINE_SHIFT_MEASURED`** — some parts record `no`, meaning a datasheet reading
   nobody has confirmed on silicon. Don't quote a width whose flag says `no` without
   saying so.
2. Measure it on hardware. The spacing at which a computed-call table's timing split
   disappears is the flash word width, read straight off the part. `flasharb` in the
   analysis half does this.

Three ways this goes wrong that are worth naming:

**The flash read width is not the cache line width**, and a datasheet will cheerfully give
you both. Take the *flash* read width. A cache-line figure gets you a `line_shift` that
unattacks a part which does in fact break.

**A T-table build usually wants the byte S-box's entry width for a last-round attack.** The
final round has no MixColumns, so most T-table implementations fall back to a byte S-box
for it: one-byte entries, and `line_off` taken from the S-box's address rather than
`Te0`'s. Reasoning from the 4-byte entry width to a 2-entry row is a correct statement
about rounds 1–9 and irrelevant to the attack being run. Which table a configuration reads
in its last round is not derivable, so **pass both shifts and let the sweep decide.**

**Don't quote a trace count without the address and offset it was measured at.** The same
firmware at three link-time accidents needed 6 250, 12 500 and 25 000 traces for the same
recovery. Every capture here records the address the device reported for exactly this
reason; a bare "needs 12 500 traces" is under-specified and somebody will try to reproduce
it.

## A statistic is not a channel

`tools/capture.py` reports a spread. [`tools/attack.py`](tools/attack.py) reports whether
the exact master key falls out and at how many traces. Only the second one is an answer. A
pooled z of −21.81 once yielded 0/16 key bytes, and that reached several documents before
anybody ran the attack on it.

**Spread does not order configurations by exploitability, and the direction is not even
fixed.** On the F401 and the G474, the table that BREAKS produces a
*smaller* spread than the table that does not. Screening cells on sigma would have
discarded the only configuration that yields a key. Elsewhere in the corpus the failure
runs the other way — a cell with sigma 3.056 is 25x cheaper to attack than one at 7.006 on
the same part. There is no safe direction to screen in.

**A large margin is not a channel either, and it scales with the spread rather than with
the information.** One cell measured a recovery margin of 108 at 25 000 traces and
recovered 0/8 key bytes, the margin falling 108 → 57 → 29 over 25 k → 100 k → 400 k —
almost exactly 1/√n.

That ratio is the test, and it needs no further captures:

* a **real** margin is in cycles and holds its absolute size as `n` grows;
* a **noise** margin falls as 1/√n.

`margin_ratio` in [`flashwhisperer/flashattack/sweep.py`](flashwhisperer/flashattack/sweep.py)
is the arithmetic.

**A partial recovery has two causes that look identical from one run and want opposite
responses.** This is the whole reason the ladder exists:

* the **pair model** can't express the pairs carrying the signal. More traces never help.
* the **trace count** is short. The model is fine.

Ladder it and watch which it does. A partial that doesn't move with `n` is the first; one
that climbs is the second. Telling them apart costs one ladder, and both have happened
here: scoring only the narrow model, neither third-party library recovered anything at
20 000 traces, where all pairs on the same capture files reach the exact key at 2 500.

**Run both pair models. Neither dominates.** The 120-pair vote reaches targets whose
round-10 reads cross a column boundary, which the 24-pair intra-column model cannot express
and reports as absent — about half of those reads, for the third-party libraries here. On a
plain byte-S-box cipher the narrow one has reached the exact key from half the traces the
wide one needed. Running only one is choosing which half of your targets to miss, which is
why `--models all,intra-column` is the default.

**The last-round adjacency is swept for you, but know that it exists.** A byte-S-box last
round is SubBytes then ShiftRows, so consecutive reads reach the ciphertext through the
inverse permutation; a T-table build that folds ShiftRows into the read emits *consecutive
ciphertext bytes* instead. The wrong convention doesn't degrade, it tests an adjacency the
access stream doesn't have — one cell elsewhere reads 7/16 under one and 16/16 at 5.3x
fewer traces under the other. `default_geometries` emits both for the narrow model and the
120-pair vote doesn't need it, so the default call covers this. **Do not narrow the models
without a reason you can write down.**

**Never infer the exploitable pair from the read order in the C.** Which pair set carries
the channel has moved with the code's residence, the optimisation level and the core clock,
and each move was first misread as a null because only a subset was scored.

## Controls, and a control that cannot fail

A 120-pair vote plus a geometry sweep has a great deal of freedom, and both can manufacture
a recovery. `controls_clean` scores captures known to contain nothing through the
**identical** call. [`tools/attack.py`](tools/attack.py) treats a capture as a control when
its cycle column is bit-exactly constant or its metadata says the tables are in RAM.

**A control that scores invalidates the RUN, not the control build, and no row of it may be
quoted.** That last line of the summary table is what makes the rest of it quotable.

Three ways a control quietly stops being one:

**A wrong-key control built by flipping a byte of the master key cannot fail cleanly.**
Seven of the sixteen round-10 bytes come out unchanged, so the control reports 7/16 on a
capture that fully breaks — and a control that reports 7/16 no matter what is not a
control. The gate's `byte_threshold` assumes the control is honestly independent. Use an
independent key and assert the round-key overlap is what you think it is.

**On a Cortex-M7, a generic SRAM region is not a RAM twin.** The L1 D-cache caches SRAM, so
tables moved to a generic buffer are still behind a cache: an H723 AXI SRAM miss costs
**+5.000** cycles and an F746 SRAM1 miss **+7.000**, while each part's DTCM is flat in
every cache configuration and is the region to use. On the ART-class parts — the F429 and
the G474, where the accelerator's cache is a *flash* cache that SRAM never reaches — this
doesn't arise, which is why the rule used to read "tables in RAM is constant time on every
ARM part here" and was wrong. **Pick the RAM region before you pick the configuration.**

**A control that was never run renders identically to a control that passed.** If you set a
knob, read it back and assert it *changed*, rather than asserting the call returned. An
empty diff, an absent grep hit and two builds that are secretly the same build all look
like a clean negative. The accelerator path here is built for this: `SS_V_SET_ACR` replies
with the **readback**, never the requested value, and `tools/capture.py` aborts rather than
filing the old setting under the new name.

And one that belongs to the capture rather than the analysis: **push the key and read it
back before every capture.** The archetypal fake null, measured on one part here: the
device keeps whatever key the last run set, a run that overrode it left the wrong key on
the board, and the next run scored those timings against the key it assumed. Result, 0/16
at every trace count from 781 to 12 500, *with a clean wrong-key control*, from a capture
that is in fact a complete break. The timings were real and the channel was there; only the
grouping was wrong. **An unexplained 0/16 should be suspected of being one of these.**

## Gate every number on the device computing the right cipher

This deserves its own section because it is the failure that looks most like a result. A
build that quietly computes the wrong function measures beautifully constant-time, and a
recovery model that assumes `SBOX⁻¹(ct ⊕ k10)` returns a **structural** null against a
function that does not compute it — a null that would have appeared whether or not a
channel existed. It has invalidated a whole set of results in the research tree before now.

[`tools/capture.py`](tools/capture.py) refuses to write a file that fails any of these, so
a file existing means they passed:

* every ciphertext against a reference AES under the victim's compiled-in key;
* the cycle column isn't all zeros — a dead counter and a constant-time cipher are
  identical in every summary statistic, and nothing encrypts a block in zero cycles;
* if an accelerator word was requested, the device's readback matched it.

The ciphertext check is the one that matters most and the one a framing or length check
would not do. The worked case is in
[`eval/lib/stcrypt/README.md`](eval/lib/stcrypt/README.md): a library framed correctly,
returned success on every call, produced a deterministic key-schedule-exact *wrong*
ciphertext for want of a peripheral clock that one line in a release note documents, and
sixty thousand traces of a mis-driven cipher would otherwise have been recorded as a
measurement.

Two more in the same family:

* **A per-capture functional control that probes the wrong function is worse than none.**
  Check the function you are measuring, at the configuration you are measuring it in.
* **A re-capture that comes back bit-identical means a stale binary was flashed.** Zero of
  250 000 samples differing is not reproducibility. Check with `np.array_equal`, not with
  summary statistics that agree to four significant figures.

## The control this harness does not give you

The research tree records **two** spreads per cell, and this repository computes only one:

| | |
|---|---|
| `chan_sd` | sd over random plaintexts. This is what `tools/capture.py` reports |
| `jitter` | sd over ~150 repeats of **one fixed** plaintext. Identical input, identical addresses, so anything it measures is environmental |

On most parts `jitter` is zero and `chan_sd` is the result. On one part at its shipped
adaptive wait states **they are the same number**, σ ≈ 22 — larger than the σ another part
gives up a whole key through, and entirely the flash controller's own timing jitter.
Quoting `chan_sd` alone there would have been the cleanest false positive the corpus has
produced. The inverse case also exists: a part whose RAM row carried σ 0.183 against a
fixed-plaintext σ of 0.116 on the same cells, which means the RAM row was sitting on the
floor and the flash rows stood far above it rather than the control being contaminated.

**So the control statement to write is "the RAM row's sd equals the fixed-plaintext
jitter", not "the RAM row is zero".** `tools/capture.py` has no fixed-plaintext mode; until
it does, a `jitter` number has to come from a short capture driven by hand, and a result
whose RAM twin is *not* bit-exactly constant cannot be read at all without one.

A related one, cheap and mostly absent from this corpus's nulls: **a sham arm.** Sweep
something that changes the stimulus and cannot reach the computation — a configuration
write that cannot affect the cipher — over the same trial structure, and check the
observable *reproduces* with no class structure. A positive control proves the instrument
can see an input-dependent effect; it does not prove the structure you are seeing comes
from the input you think. On a self-timed on-chip counter the second branch is the one that
bites: a counter that has stopped while its enable bit still reads set makes every interval
come out constant, and a sham arm catches that as a non-reproducing observable where a
positive control does not. It costs minutes. A 0/16 with a sham arm beside it is a strictly
stronger claim than a 0/16 without one.

## Refuted shortcuts — do not re-derive these

Every row cost bench time, several reached multiple documents before anyone checked, and
all of them are the kind of thing an evaluation reaches for when it wants a conclusion
cheaply.

| claim | verdict |
|---|---|
| *"A cache big enough to hold the table makes the part safe."* | **False.** A part with a 32 KB L1 D-cache — many times its T-table, resident long before the encryption ends — still gives up the whole key. On that part turning the I-cache on made it *both* easier to attack and faster |
| *"So a cache is the dangerous knob."* | **Also per-part.** On the F746 the D-cache CLOSES the channel, a 256-byte S-box fitting in its 4 KB; on the H723 turning a cache on opened it wider. Same vendor, same core, same cipher, and the safe setting on one part is the dangerous one on the other. Measure the part you have |
| *"This is an ST / flash-accelerator property."* | **False.** A part with no data cache, no `FLASH_ACR` and no wait-state field anywhere in its memory map gives up the full master key in a few thousand traces. Another vendor's part has an instruction-only cache that provably does not carry the channel, and leaks anyway |
| *"Wait states are the information."* | **No — a gain factor.** The per-collision signal is `LATENCY` cycles where a part has both a read latch and a measured differential, and the channel exists with no wait-state axis at all |
| *"So sweeping the wait state covers the read path."* | **Not necessarily — enumerate the whole controller.** One part's read-margin field, sharing a register with its wait states, costs more on a whole encryption than the wait state does |
| *"Turning the flash accelerator off is a defence."* | **Usually a discount.** On most of the library builds measured on one part the last-round attack got 2.5x to 6x *cheaper* with the caches off |
| *"Aligning the table to the row size mitigates it."* | **No, and the DIRECTION is per-part, so neither alignment is the safe one.** Alignment moves the trace count by 2–4x and removes nothing: any 256-byte table spans at least 16 rows on a 16-byte-granule machine. One part is cheapest at offset 0; another, in the cleaner experiment of two builds differing in nothing but a pad size, is cheapest at offset 8. Sweep it and quote it |
| *"Run the cipher from RAM."* | **Half right, and the costly half is the wrong half.** Moving only the *code* to SRAM still gave up the exact master key and cost 50% runtime. Moving only the *tables* was constant-time *and* the fastest of the four placements |
| *"Change the clock and the channel changes."* | **False at fixed wait states.** Wait states are counted in core clocks, so at matched `LATENCY` the whole channel is invariant — one part reproduced every moment and the entire recovery ladder bit-identically across a 10.6x clock ratio. Not a mitigation axis and not a confound to control for |
| *"Upstream library defaults tell you what shipped firmware does."* | **No.** Check where the given build puts its tables |
| *"Sigma orders cells by exploitability."* | **No, in either direction.** See *A statistic is not a channel* above |
| *"A flat cell in a short screen is a null."* | **No — it is an under-sampled cell.** One cell read a single distinct value at sd 0.000 over 2 048 traces, sd 3.056 at 8 192, and gave up half the key bytes at a prefix of 2 000 inside a 100 000-trace capture. The screen chooses what to capture deeply; it does not decide anything |
| *"0/16 is how you report a negative."* | **Not on its own.** Quote the ladder, the margin's scaling, and the effect size the capture could have resolved |
| *"This is a new attack."* | **The cryptanalysis is prior art end to end** — the expanded final-round attack is Bonneau–Mironov (CHES 2006), finishing from round-2 collisions is Osvik–Shamir–Tromer and Acıiçmez–Schindler–Koç. What is new here is the *mechanism* and the per-part measurements, so lead with those |
| *"The flash-table channel in this library is something to disclose as new."* | **Check the library's own threat model first.** At least one library in this tree says in its `SECURITY.md` that its software AES lookup tables are vulnerable to timing attacks, including remote ones depending on latency. A report that leads with novelty there is wrong on the facts, and it is an avoidable way to lose a disclosure conversation |

Two meta-rules from the same place, because promoting a finding is the moment the wrong
rows went wrong:

* **Check a claim against the part that BOUNDS it, not only the part that established it.**
  *"At zero wait states the channel does not exist"* reached seven documents before anyone
  checked the two parts that carry a second channel there.
* **Where two documents disagree, do not average them.** Find which one has the evidence,
  keep it, and say in the survivor what it replaced, so the next reader does not re-derive
  the loser.

## What a green run is not checking

Both checkers in this tree carry their own blind spots in their headers, which is the house
style. The short version:

* **CI measures nothing.** There is no board, so no capture runs, no ciphertext is checked
  and no recovery is attempted. Every claim here about whether something leaks rests on a
  bench run. The CI toolchain is also not the bench toolchain, and a codegen difference
  changes the instruction stream — which is part of what is being measured — so a CI build
  is a syntax-and-layout check rather than a reproduction.
* [`tools/check_tables_move.sh`](tools/check_tables_move.sh) checks **one** library, the
  in-tree control, and **one** of the three slots. The pairs that matter to a result are
  the third-party libraries, and none of them is looked at. It also builds nothing — it
  reads what `make matrix` left behind, so a part whose matrix predates a change still
  reports the old addresses.
* [`tools/verify.sh`](tools/verify.sh) cannot see whether a library was *compiled* the way
  an axis needs. `-fdata-sections` is what makes the residence axis exist; `make addr`
  reading 0 for every slot is the symptom, and this script will not show it.

When you add a check, write down in its docstring or header what a green result is **not**
looking at. Every check in the research tree that went quietly wrong failed the same way,
by testing one half of the thing — a link checker that validated each link's target and
ignored its label, for 640 links.

## Known stale claims in this tree

Fix these when you are next in the file rather than working around them, and say in the
survivor what it replaced.

* [`docs/DESIGN.md`](docs/DESIGN.md) says of `eval/mcu/<part>/eval.mk` that "NOTHING READS
  EITHER OF THEM". [`tools/attack.py`](tools/attack.py) reads `EVAL_LINE_SHIFT` from it and
  raises naming the file when it is absent, so that section is out of date and the
  conclusion it draws ("collapsing the directory is the smaller half") no longer follows.
* [`instrumentation/victim/include/ss_victim.h`](instrumentation/victim/include/ss_victim.h)
  says the linker slots are defined in `eval/lib/<name>/tables.ld`. The files are
  `tab_t.ld`, `tab_s.ld` and `tab_d.ld`.
* A port that **builds** is not a port that **measures**, and the roster is split on
  exactly that line. Most parts here have never been on a board; each of those says so in
  its `board.c` opening paragraph. Do not read a part's presence in `make list` as evidence
  that anything was measured on it.

## Writing things in here

* **Line endings are LF in the working tree as well as the object database**, pinned by
  `.gitattributes`, because the firmware is built under a POSIX shell. If you rewrite a
  file with a script, open it in **binary** mode, or a 14-line change lands as a whole-file
  diff.
* **Don't use a bash heredoc to write source.** Quoted delimiters don't protect backslash
  continuations. Use the editor tools.
* **Any markdown file an LLM writes gets the `*🤖WARNING🤖: ...*` line under the title**, in
  that exact wording, visible rather than an HTML comment. The `*🤖🥩: ...*` variant means a
  human has since read the file line by line, which only the maintainer can say, so never
  write that one and never promote a file from the first marker to the second.
* **Don't write a count in prose.** Not of parts, not of libraries, not of files, and not as
  an ordinal. Every such count in the research repo was wrong within days — three separate
  dossiers each called their part "the project's eighth part", with two different meanings.
  `make list` is the only place that counts parts and libraries here. Everywhere else, say
  what a thing is the **first** of, which does not go stale, and link the roster.
* **State the configuration in full** when you report a number — clock, wait states,
  accelerator word, table address and offset, trace count — because every one of them
  changes the answer. And **distinguish measured from inferred** at the point of use: more
  than one wrong entry in the research tree came from a register value read out of source
  with its consequence assumed rather than probed.
* **A quiet result means "not at this trace count".** It never means safe.
* **Commit messages:** one line, imperative, no conventional-commit prefix, doubts in
  parentheses. Attribution trailers are wanted.

## Open ends worth knowing about

* **The off-chip instrument.** `instrumentation/sstimer/` is empty; the driver firmware and
  its host library haven't been moved over. Until they are, every result here is self-timed
  and carries that objection.
* **The library that ships as a binary archive.**
  [`eval/lib/stcrypt/README.md`](eval/lib/stcrypt/README.md) is reserved and not wired up. It is
  worth having for one reason the others can't give: it is the case that shows the
  table-residence control works with no source at all. Its README already records the three
  things that would otherwise cost a bench session.
* **Non-Cortex-M parts.** Nothing in the protocol or the host assumes ARM and the counter
  hook is wide enough for a RISC-V `mcycle` or a timer peripheral, but none has been
  ported, so that is an argument rather than a measurement. What is still untested in the
  board contract is a counter that is **not** at core rate.
* **The full catalogue of confounds** that manufacture or hide a timing channel is a
  document in the private research repo, so you won't have it. What is above is the subset
  that bears on using this harness. If a measurement surprises you, **suspect the harness
  before the part.**
