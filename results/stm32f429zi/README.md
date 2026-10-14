# STM32F429ZI — the second part, and a trace-count curve

NUCLEO-F429ZI, Cortex-M4F at 16 MHz (HSI, no PLL), `FLASH_ACR = 0x700`
(prefetch, I-cache and D-cache on, zero wait states). On-chip arm, DWT
CYCCNT. The reference byte-S-box cipher.

This part was ported to test whether the board contract was real or
whether it had been shaped around the first part. It differs from it in
all three places the contract has hooks for — a four-bit wait-state field
with two resettable caches, a counter that counts **up** instead of down,
and a different console — and adding it changed nothing outside
`mcufw/stm32f429zi/` except teaching `TIMING=` the value `dwt`.

> **This file was wrong on its first writing too.** It said no key had
> been recovered. One has: the first run was simply at a fifth of the
> traces this part needs.

## The trace-count curve

250 000 traces captured, every ciphertext checked, scored at `line_shift
= 4` (this part's flash is read 16 bytes wide):

| n | 120-pair | 24-pair |
|---:|---|---|
| 25 000 | 2/16 | 2/16 |
| 50 000 | 10/16 | 6/16 |
| **100 000** | **16/16 — EXACT MASTER KEY** | 14/16 |

**So on this part it genuinely was the trace count.** The 20 000-trace
run reported here first scored 3/16, which sits exactly on this curve.
Unlike the first part — where the third-party libraries were blocked by
the attack's pair model and more data would not have helped — here the
model was fine and the data was short.

The two halves of that are worth keeping straight, because they look
identical from a single failed run: a partial recovery that does not
improve with data is a model problem, and one that climbs is a data
problem. The way to tell them apart is a ladder, which costs one capture.

`line_shift = 2` gives 1/16 at every count, so the sweep also establishes
that this last round reads the byte S-box rather than a word table.

## The accelerator is an axis, and the device confirms it took

| `FLASH_ACR` | | cycles sd | distinct |
|---|---|---|---|
| `0x700` | prefetch + both caches, **0 wait states** | 3.8568 | 35 |
| `0x705` | the same, **5 wait states** | 31.5863 | 216 |

Eight times the spread at five wait states. `SS_V_SET_ACR` replies with
the **readback**, so a capture records the configuration the device
actually entered, and `tools/capture.py` aborts if the device refused.

Note that the larger spread is not obviously the easier target: the
recovery above is from the `0x700` arm. Wait states are a gain factor on
the measurement, not more information.

## The RAM twin: non-zero, and still null

| | cycles sd | distinct | recovery |
|---|---|---|---|
| tables in flash | 3.8568 | 35 | exact key at 100 000 |
| tables in RAM | 0.0224 | 2 | **0/16 under both models** |

The twin here is **not** bit-exactly constant, unlike the first part's —
and that was flagged as unexplained when this file was first written. It
is now scored rather than wondered about: sd 0.0224 over two distinct
values, and **0/16 key bytes** under both pair models.

That is the stronger form of the control. A twin whose spread is not zero
is still a valid control if its *recovery* is null — which is this
project's own rule, a statistic is not a channel, applied to the control
instead of to the result. So this part does carry a small
data-dependent timing component that is not the flash, and that component
does not carry the key. The flash does.

## The port's first run was wrong, and silently

Worth keeping, because the failure produced numbers rather than an error:

```
cyc: mean 0.000  sd 0.0000  min 0  max 0  distinct 1
ident: ... cyc=1
```

`DEMCR.TRCENA` is cleared by the debug probe when it **disconnects**,
which happens after the board has booted and run its own init. So arming
CYCCNT in `ssb_init` arms it and then has it switched off — and the
liveness check in `ssb_init` ran before that and passed, leaving the
device reporting a working counter it did not have. It is re-armed in
`ssb_get`, which runs once per received byte and is outside every
measured region.

`tools/capture.py` now refuses a capture whose cycle column is all zeros:
a dead counter and a constant-time cipher are identical in every summary
statistic, and nothing encrypts a block in zero cycles.

## What is not here

* **Only the reference cipher.** Mbed-TLS and wolfSSL build for this part
  and are in `make matrix`, but have not been captured on it.
* **Only one accelerator word scored.** The `0x705` arm was captured and
  not laddered.
* **No off-chip arm**, and no `DCEN=0` arm — turning the data cache off
  is one accelerator word away and is the obvious next variable.
