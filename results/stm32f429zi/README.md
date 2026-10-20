# STM32F429ZI - the second part, and a trace-count curve

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

NUCLEO-F429ZI, Cortex-M4F at 16 MHz (HSI, no PLL), `FLASH_ACR = 0x700`
(prefetch, I-cache and D-cache on, zero wait states). On-chip arm, DWT
CYCCNT. The reference byte-S-box cipher.

This part got ported to test whether the board contract was real or
whether it had been shaped around the first part. It differs from it in
all three places the contract has hooks for - a four-bit wait-state field
with two resettable caches, a counter that counts UP instead of down, and
a different console - and adding it changed nothing outside
`mcufw/stm32f429zi/` except teaching `TIMING=` the value `dwt`.

**NOTE:** this file was wrong on its first writing too. It said no key had
been recovered. One has; the first run was simply at a fifth of the traces
this part needs.

## The trace-count curve

250 000 traces captured, every ciphertext checked, scored at `line_shift
= 4` (this part's flash is read 16 bytes wide):

| n | 120-pair | 24-pair |
|---:|---|---|
| 25 000 | 2/16 | 2/16 |
| 50 000 | 10/16 | 6/16 |
| 100 000 | 16/16 - EXACT MASTER KEY | 14/16 |

So on this part it genuinely was the trace count. The 20 000-trace run
reported here first scored 3/16, which sits exactly on this curve. Unlike
the first part - where the third-party libraries were blocked by the
attack's pair model and more data would not have helped - here the model
was fine and the data was short.

The two halves of that are worth keeping straight, since they look
identical from a single failed run: a partial recovery that doesn't
improve with data is a model problem, and one that climbs is a data
problem. The way to tell them apart is a ladder, which costs one capture.

`line_shift = 2` gives 1/16 at every count, so the sweep also establishes
that this last round reads the byte S-box rather than a word table.

## The accelerator is an axis, and the device confirms it took

| `FLASH_ACR` | | cycles sd | distinct |
|---|---|---|---|
| `0x700` | prefetch + both caches, 0 wait states | 3.8568 | 35 |
| `0x705` | the same, 5 wait states | 31.5863 | 216 |

Eight times the spread at five wait states. `SS_V_SET_ACR` replies with
the readback, so a capture records the configuration the device actually
entered, and `tools/capture.py` aborts if the device refused.

Note that the larger spread isn't obviously the easier target: the
recovery above is from the `0x700` arm. Wait states are a gain factor on
the measurement, not more information.

## The RAM twin: non-zero, and still null

| | cycles sd | distinct | recovery |
|---|---|---|---|
| tables in flash | 3.8568 | 35 | exact key at 100 000 |
| tables in RAM | 0.0224 | 2 | 0/16 under both models |

The twin here is NOT bit-exactly constant, unlike the first part's, and
that was flagged as unexplained when this file was first written. It's now
scored rather than wondered about: sd 0.0224 over two distinct values, and
0/16 key bytes under both pair models.

That's the stronger form of the control. A twin whose spread isn't zero is
still a valid control if its *recovery* is null, which is this project's
own rule (a statistic is not a channel) applied to the control instead of
to the result. So this part does carry a small data-dependent timing
component that isn't the flash, and that component doesn't carry the key.
The flash does.

## The port's first run was wrong, and silently

Worth keeping, since the failure produced numbers rather than an error:

```
cyc: mean 0.000  sd 0.0000  min 0  max 0  distinct 1
ident: ... cyc=1
```

`DEMCR.TRCENA` is cleared by the debug probe when it DISCONNECTS, which
happens after the board has booted and run its own init. So arming CYCCNT
in `ssb_init` arms it and then has it switched off - and the liveness
check in `ssb_init` ran before that and passed, leaving the device
reporting a working counter it didn't have. It's re-armed in `ssb_get`,
which runs once per received byte and is outside every measured region.

`tools/capture.py` now refuses a capture whose cycle column is all zeros:
a dead counter and a constant-time cipher are identical in every summary
statistic, and nothing encrypts a block in zero cycles.

## LibTomCrypt on the same board

The pinned upstream commit is `6c6d5104de66f3ca0dfd7b68540ef86869982b07`.
These are new captures from this repository's harness, not numbers imported
from the earlier research image. All three builds used the NUCLEO-F429ZI at
16 MHz, DWT CYCCNT, warm-up enabled, and `FLASH_ACR=0x705` (read back by the
device). Every ciphertext was checked against reference AES-128. The capture
files are local and ignored by Git, as described in `results/README.md`.

| LibTomCrypt build | Mean cycles | SD | Traces | Recovery at 2.5k / 5k / 10k / 25k |
|---|---:|---:|---:|---|
| Default 4T + four final `Te4_*` tables, flash | 2241.788 | 9.6627 | 50,000 | 2/16, 9/16, **16/16 verified**, 16/16 |
| `LTC_SMALL_CODE`: one `TE0` + one `Te4`, flash | 2015.084 | 19.4021 | 50,000 | 0/16, 1/16, 7/16, **16/16 verified** |
| Default tables moved to RAM | 1319.000 | 0.0569 | 25,000 | 0/16, 0/16, 0/16, 0/16 |

The default and compact flash builds also verify at 50,000 traces. No
brute-force candidates were used. The scorer uses 24 same-byte-lane pairs
for the default final round's four separate tables, and all 120 pairs for
the compact build's shared final-round table. The F429 has a 16-byte flash
read row, giving a two-bit *word-index* row shift and offset zero. The RAM
control uses the default AES code at the identical `0x08003a8c` address;
only its table residence changes (`0x08000400` to `0x20000000`).

The exact ladder output is in [`libtomcrypt_attack.log`](libtomcrypt_attack.log).
Reproduce it with `python tools/attack_libtomcrypt.py
results/stm32f429zi/libtomcrypt*.npz`. The three local capture SHA-256s,
in default-flash, default-RAM, compact-flash order, are:

```
E8D8D1F96BC35915374FAF85DA948888702E19F3BD8092AEBC3765413DB52A80
B70CE026861AEB5FF2E450CFF9D5E893FF056C4833012E1CEDBB52B08DA27CC0
0BE0628937EC35DA367BD9B4734EC2206E0F9BD5200AD85F2C80C3E5EB2F581C
```

## What is not here

* Mbed-TLS and wolfSSL build for this part and are in `make matrix`, but
  haven't been captured on it.
* Only one accelerator word scored. The `0x705` arm was captured and not
  laddered.
* No off-chip arm, and no `DCEN=0` arm - turning the data cache off is one
  accelerator word away, and is the obvious next variable.
