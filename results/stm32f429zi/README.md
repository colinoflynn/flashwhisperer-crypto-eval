# STM32F429ZI — the second part, as an instrument check

NUCLEO-F429ZI, Cortex-M4F at 16 MHz (HSI, no PLL). On-chip arm, DWT
CYCCNT. The reference byte-S-box cipher only.

**This part was ported to test the abstraction, not to produce a result**,
and it should be read that way. It differs from the first part in all
three places the board contract has hooks for — a four-bit wait-state
field with two resettable caches instead of one latency bit and no cache,
a counter that counts UP instead of DOWN, and a different console — and
adding it changed nothing outside `mcufw/stm32f429zi/` except teaching
`TIMING=` the value `dwt`.

## What was measured

| `FLASH_ACR` | what it is | cycles sd | distinct | exact key |
|---|---|---|---|---|
| `0x700` | prefetch + I-cache + D-cache, **0 wait states** | 3.8791 | 31 | no, 1/16 at 20 000 |
| `0x705` | the same, **5 wait states** | 31.5863 | 216 | no, 3/16 at 20 000 |
| `0x700`, tables in RAM | the twin | 0.0224 | 2 | — |

20 000 traces each except the twin (2 000). `line_shift = 4` — this part's
flash is read 16 bytes wide. Every ciphertext checked.

## What this does and does not establish

**The port works as an instrument.** It measures, the ciphertexts are
right, the accelerator is settable at run time with the device confirming
by readback, wait states move the spread by a factor of eight, and the RAM
twin collapses it by about 170×. The board contract did not need widening
to accommodate a part that differs from the first in every hook.

**No key has been recovered from it.** Three of sixteen bytes at 20 000
traces at five wait states, one of sixteen at zero. The spread is larger
than the first part's — 31.59 against 2.20 — and yields less, which is the
clearest demonstration in this repository that **a statistic is not a
channel**.

The first thing to vary is the data cache. It is enabled in both arms
above, and the ART's cache is not the flat 16-byte row the attack was told
to assume — so `line_shift = 4` may be describing the flash and not the
structure the lookups actually collide in. Turning `DCEN` off is one
accelerator word away and has not been tried.

**The twin is not bit-exact here**, unlike on the first part: 0.0224 with
two distinct values rather than 0.0000 with one. That is a small residual
and it has not been explained. It should be, before any null from this
part is quoted as a null.

## The port's first run was wrong, and silently

Worth recording because the failure produced numbers rather than an error:

```
cyc: mean 0.000  sd 0.0000  min 0  max 0  distinct 1
ident: ... cyc=1
```

`DEMCR.TRCENA` is cleared by the debug probe when it **disconnects**,
which happens after the board has booted and run its own init. So arming
CYCCNT in `ssb_init` arms it and then has it switched off, and the
liveness check in `ssb_init` ran before that and passed — leaving the
device reporting a working counter it does not have. It is re-armed in
`ssb_get` instead, which is outside every measured region.

`tools/capture.py` now refuses a capture whose cycle column is all zeros.
A dead counter and a constant-time cipher are identical in every summary
statistic, and nothing encrypts a block in zero cycles.
