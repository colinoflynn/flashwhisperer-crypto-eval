# STM32L152RE — the first part measured in this tree

NUCLEO-L152RE, Cortex-M3 at 32 MHz, `FLASH_ACR = 0x07` (64-bit access,
prefetch on, one wait state — the only in-spec configuration at this
clock). On-chip arm: the victim times its own encryption with SysTick.
Every ciphertext checked against a reference AES before anything was
written.

> **This file was wrong on its first writing** and the correction is the
> most useful thing in it. It reported that neither third-party library
> recovered a key. They both do. What was missing was pairs the attack
> could not express — see *What the first reading got wrong* below.

## What was measured

Recovery is the last-round attack with `line_shift` and the pair model
both swept. "exact key" is the full AES-128 master key, obtained by
inverting the recovered round-10 key and comparing with the truth.

| library | configuration | table | cycles sd | exact key at |
|---|---|---|---|---|
| reference | byte S-box, in flash | `0x08001400` | 2.1991 | **5 000** (24-pair) |
| reference | four T-tables, in flash | `0x08001400` | 0.6717 | **2 500** (120-pair) |
| reference | byte S-box, **tables in RAM** | `0x20001000` | 0.0000 | — control |
| Mbed-TLS 3.6.7 | **stock** | none in flash | 0.0000 | — no channel |
| Mbed-TLS 3.6.7 | `ROM_TABLES` | `0x08001400` | 0.7828 | **2 500** (120-pair) |
| Mbed-TLS 3.6.7 | `ROM_TABLES` + `FEWER_TABLES` | `0x08000800` | 1.7666 | **5 000** (120-pair) |
| Mbed-TLS 3.6.7 | `ROM_TABLES`, **tables in RAM** | `0x20001000` | 0.0000 | — control |
| wolfSSL 5.9.4 | stock | `0x08000400` | 0.5841 | **never — 6/16, see below** |
| wolfSSL 5.9.4 | `SMALL_TABLES` | `0x08000400` | 2.1918 | **10 000** (120-pair) |
| wolfSSL 5.9.4 | stock, **tables in RAM** | `0x20000000` | 0.0000 | — control |

**Four controls, all clean**: every bit-exactly constant build and every
RAM twin was scored with the identical call and recovered nothing. That
check is not decoration — the 120-pair vote has five times the freedom of
the 24-pair one, so it can manufacture a result, and a control that
scored would invalidate the whole table.

## What the first reading got wrong

The first pass scored only the **24 intra-column** byte pairs reachable
from six row pairings, and under it the reference cipher gave the exact
key at 5 000 traces while both third-party libraries gave nothing at
20 000. That was written up here as a property of the libraries.

It was a property of the attack. About half of these libraries'
consecutive round-10 reads **cross a column boundary**, and the 24-pair
model cannot express those pairs, so it reports them as absent. Scoring
all 120 output-byte pairs with a per-byte vote — same capture files, same
traces, same plaintexts — takes Mbed-TLS to the exact key at **2 500**.

**More traces was the wrong lever**, and the data says so without any
further captures: a real margin is in cycles and holds its absolute size
as n grows, while a noise margin falls as 1/√n.

**And neither model dominates.** On the reference byte-S-box cipher the
*narrow* model reaches the exact key at 5 000 where the 120-pair vote gets
14/16 at the same count. Both are run and the table says which won.

## The one that still does not fall: wolfSSL stock

**6/16 key bytes at 20 000, at 50 000 and at 100 000 traces.** Five times
the data moves nothing, and the 24-pair model gets 0–1/16 throughout.

So for this configuration it is neither the pair model nor the trace
count. It reproduces exactly the 6/16 an earlier campaign reached on this
part. What distinguishes it from its own `SMALL_TABLES` sibling — which
recovers completely at 10 000 — is not established here.

Note which way round that is: the configuration with the **larger** tables
and the **smaller** spread is the one that resists. "Not demonstrated" is
the honest reading, not "safe".

## Four things this says

**Stock Mbed-TLS has no flash-timing channel on this part, and that is not
our doing.** `MBEDTLS_AES_ROM_TABLES` defaults OFF, so the tables are
generated into `.bss` at run time. The device reports **address 0 for all
three table slots** and the count is bit-exactly constant. A negative
control that came with the library, with the positive control one flag
away.

**wolfSSL has no such default.** Its tables are `static const` and in
flash in every build, so there is no quiet configuration to compare
against — the link-time twin is its only residence axis.

**The reduced-table options move the spread UP, and the trace count
both ways.** `FEWER`/`SMALL_TABLES` roughly doubles the spread in both
libraries. For Mbed-TLS it makes recovery *harder* (2 500 → 5 000); for
wolfSSL it is the difference between recovering at 10 000 and not
recovering at all. A smaller table is not a smaller target.

**Every RAM twin is bit-exactly constant and recovers nothing.** The flash
is producing the variation, not the instrument.

## `line_shift` is load-bearing, and the sweep found it

Every row above recovers at `line_shift = 3` — eight 1-byte entries per
8-byte flash row — and scores **0/16 at `line_shift = 1`** on the same
capture. That is the attack discovering that these last rounds read the
**byte S-box**, not the 4-byte word table, which is a property of each
library's last round rather than of the part. Guessing it wrong returns a
clean null that reads exactly like a part that does not leak.

## Parity with the rig this generalises

| | that rig | here |
|---|---|---|
| reference S-box, table address | `0x08001400` | `0x08001400` |
| reference S-box, RAM twin | `0x20001000` | `0x20001000` |
| reference S-box, cycles sd | 2.2033 | 2.1991 |
| reference S-box, exact key | 5 000 | 5 000 |
| Mbed-TLS `ROM_TABLES`, table | `0x08001400` | `0x08001400` |
| Mbed-TLS `+FEWER`, table | `0x08000800` | `0x08000800` |
| wolfSSL, table | `0x08000400` | `0x08000400` |
| Mbed-TLS and wolfSSL `SMALL_TABLES` under 120 pairs | exact | exact |
| wolfSSL stock under 120 pairs | 6/16 | 6/16 |

Every table address identical, the spread agreeing to within the
difference between two trace counts, the recovery landing at the same
number — **and the one partial result landing on the same 6/16**.

## What is not here

* **The off-chip arm.** All of this is the victim timing itself.
* **The accelerator sweep.** Everything is at `FLASH_ACR = 0x07`; the
  other in-spec words are listed in `eval/mcu/stm32l152re/eval.mk`.
* **ST's Cryptographic Library** — a byte S-box shipped as a binary
  archive, and on an earlier campaign the configuration that gave up the
  exact key most readily. See `eval/lib/stcrypt/README.md`.
* **Why wolfSSL stock resists.** The open question on this part.
