# STM32L152RE — the first part measured in this tree

NUCLEO-L152RE, Cortex-M3 at 32 MHz, `FLASH_ACR = 0x07` (64-bit access,
prefetch on, one wait state — the only in-spec configuration at this
clock). On-chip arm: the victim times its own encryption with SysTick.
20 000 traces per configuration, seed 1, every ciphertext checked against
a reference AES.

**This part is the reference.** It is the floor case — one latency bit, no
cache of any kind — and a measured baseline for it already existed, so it
is what says whether this tree measures the same thing the rig it
generalises did. It does; see *Parity* below.

## What was measured

| library | configuration | table | cycles sd | distinct | exact key |
|---|---|---|---|---|---|
| reference | byte S-box, tables in flash | `0x08001400` | 2.1991 | 17 | **5 000** |
| reference | four T-tables, in flash | `0x08001400` | 0.6717 | 6 | **2 500** |
| reference | byte S-box, **tables in RAM** | `0x20001000` | 0.0000 | 1 | — |
| Mbed-TLS 3.6.7 | **stock** | — | 0.0000 | 1 | — |
| Mbed-TLS 3.6.7 | `ROM_TABLES` | `0x08001400` | 0.7828 | 7 | not at 20 000 |
| Mbed-TLS 3.6.7 | `ROM_TABLES` + `FEWER_TABLES` | `0x08000800` | 1.7666 | 14 | not at 20 000 |
| Mbed-TLS 3.6.7 | `ROM_TABLES`, **tables in RAM** | `0x20001000` | 0.0000 | 1 | — |
| wolfSSL 5.9.4 | stock | `0x08000400` | 0.5841 | 7 | not at 20 000 |
| wolfSSL 5.9.4 | `SMALL_TABLES` | `0x08000400` | 2.1918 | 17 | not at 20 000 |
| wolfSSL 5.9.4 | stock, **tables in RAM** | `0x20000000` | 0.0000 | 1 | — |

Recovery is the last-round attack at `line_shift = 3` — this part's flash
is read 64 bits wide — with the table offset taken from the address the
device reported, both adjacencies swept and all six pair sets unioned.

## Four things this says

**Stock Mbed-TLS has no flash-timing channel on this part, and that is not
our doing.** `MBEDTLS_AES_ROM_TABLES` defaults OFF, so the tables are
plain `static` arrays generated into `.bss` at run time. The device
reports **address 0 for all three table slots** and the cycle count is
bit-exactly constant over 20 000 traces. This is a negative control that
*came with the library*, and the positive control is the same library one
flag away.

**wolfSSL has no such default.** Its tables are `static const` and in
flash in every build, so there is no quiet configuration to compare
against — the link-time twin is the only residence axis it has.

**The reduced-table options make the spread LARGER, not smaller.** Both
libraries' "fewer tables" flag roughly doubles the spread and the number
of distinct cycle counts, on both libraries, in the same direction. A
smaller table is not a smaller target here; it concentrates the lookups
into fewer flash rows.

**Every RAM twin is bit-exactly constant.** sd 0.0000, one distinct value
over 20 000 traces, in all three cases. The flash is producing the
variation, not the instrument.

## And one thing it does not say

**No key was recovered from either third-party library** at 20 000 traces,
in any configuration, despite spreads comparable to the reference cipher's
— wolfSSL `SMALL_TABLES` has sd 2.1918 against the reference's 2.1991 and
yields nothing while the reference yields the exact key at 5 000.

That is the point of running the recovery: **a statistic is not a
channel**, and a spread of the same size can be worth everything or
nothing. What distinguishes them has not been established here, and the
honest reading is "not demonstrated at this trace count with this
attack", not "safe".

## Parity with the rig this generalises

The question this part exists to answer: does the restructured tree
measure the same thing the single-part rig did?

| | that rig | here |
|---|---|---|
| reference S-box, table address | `0x08001400` | `0x08001400` |
| reference S-box, RAM twin address | `0x20001000` | `0x20001000` |
| reference S-box, cycles sd | 2.2033 | 2.1991 |
| reference S-box, exact key | 5 000 traces | 5 000 traces |
| Mbed-TLS `ROM_TABLES`, table | `0x08001400` | `0x08001400` |
| Mbed-TLS `+FEWER`, table | `0x08000800` | `0x08000800` |
| wolfSSL, table | `0x08000400` | `0x08000400` |
| Mbed-TLS and wolfSSL, exact key | none at 50 000 | none at 20 000 |

Every table address is identical, the spread agrees to within the
difference between two trace counts, and the recovery lands at the same
number. The two also agree on the negative: that rig recovered nothing
from either third-party library at **50 000** traces, so the nulls here at
20 000 are the established result rather than a shortfall of this tree.

## What is not here

* **The off-chip arm.** Everything above is the victim timing itself,
  which is the best case for an attacker and the easiest thing for a
  vendor to dismiss. The instrument that removes the victim from the
  measurement has not been moved into this repository yet.
* **The accelerator sweep.** Everything is at `FLASH_ACR = 0x07`. The
  other in-spec words on this part are listed in
  `eval/mcu/stm32l152re/eval.mk` and have not been walked.
* **ST's Cryptographic Library**, which on this part is a byte S-box
  implementation shipped as a binary archive — the configuration most
  likely to behave like the reference cipher, and the one missing from the
  table above. See `eval/lib/stcrypt/README.md`.
* **Trace counts beyond 20 000.** The three library nulls are nulls at
  20 000, and the ladder stops there.
