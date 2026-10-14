# STM32H723ZG - the third part, and the one that bends the contract

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

NUCLEO-H723ZG, Cortex-M7 at 64 MHz (HSI, no PLL), SysTick, the reference
byte-S-box cipher. On-chip arm. Every ciphertext checked against a
reference AES before anything was written.

This part was ported because the first two did not really test the board
contract. They are a one-bit latency field and a four-bit one, both with
their caches behind `FLASH_ACR`. This part has no accelerator bits in
`FLASH_ACR` at all: every cache on the data path is the Cortex-M7 L1, over
in the core's own `SCB`. So the single opaque accelerator word is PACKED -
latency in bits 0..3, I-cache in bit 8, D-cache in bit 9 - which is the
escape hatch `ss_board.h` documents for exactly this case.

Adding it changed nothing outside `mcufw/stm32h723zg/` and
`eval/mcu/stm32h723zg/`. `make list` and CI both pick a part up from the
directory alone, so neither needed editing.

## What was measured

25 000 traces per arm, scored with `line_shift` and the pair model both
swept. "Exact key" is the AES-128 master key, from inverting the recovered
round-10 key and comparing with the truth.

| configuration | acr | table | mean | cycles sd | distinct | recovery |
|---|---|---|---:|---:|---:|---|
| both L1 caches off, the reset state | `0x7` | `0x08001400` | 10 206.8 | 20.9992 | 149 | 12/16, and flat |
| I-cache on, D-cache off | `0x107` | `0x08001400` | 4 831.8 | 29.3507 | 208 | exact key at 5 000 |
| tables in DTCM, caches off | `0x7` | `0x20001000` | 8 427.0 | 0.0000 | 1 | control, clean |

## The production configuration is the one that falls

I-cache on with D-cache off is what production H7 firmware routinely
ships, and it is the worst cell here by a distance:

| n | both caches off | I-cache on |
|---:|---|---|
| 2 500 | 10/16 | 6/16 |
| 5 000 | 12/16 | 16/16 - EXACT MASTER KEY |
| 10 000 | 12/16 | - |
| 25 000 | 12/16 | - |

Turning a cache ON made the part easier to attack, not harder. It also
more than halves the execution time, 10 207 cycles to 4 832, so this is
not a configuration anyone would avoid on performance grounds either.

This reproduces independently, in this tree, what the research repository
already records for the part: a large cache does not make a part safe.

## The reset configuration does NOT fall, and that is a model problem

With both caches off the recovery reaches 12/16 and then stops:

```
n= 2 500   10/16      n=10 000   12/16
n= 5 000   12/16      n=25 000   12/16
```

Five times the data moves nothing. By this project's own rule a partial
that does not improve with data is the MODEL, not the trace count, so
more traces are the wrong lever here - the same shape as wolfSSL stock on
the L152.

One explanation is already ruled out without spending a capture: the
S-box is at `0x08001400` and `0x1400` is an exact multiple of 32, so the
table starts on a flash-row boundary and `line_off` is 0. Sweeping the
offset cannot be the answer. What those four bytes have in common is not
established, and "not demonstrated" is the honest reading rather than
"safe" - the arm one bit away gives up the whole key at 5 000.

## `line_shift` is 5 here, and the sweep found it

This part reads its flash 32 bytes at a time, the widest of the three, so
a byte S-box wants `line_shift = 5` where the F429 wants 4 and the L152
wants 3. The sweep establishes it rather than taking it on trust: at
2 500 traces `ls=5` scores 10/16 and `ls=3` scores 0/16 on the same file.

By 25 000 `ls=3` catches up to the same 12/16, which is what you would
expect from a partition that is a strict refinement of the right one -
given enough data the coarser structure still shows through. At the trace
counts that matter, the wrong width is a clean null.

And neither pair model dominates, again: the exact key came from the
24-pair state-order model at 5 000, where the 120-pair vote had 12/16 on
the same capture.

## The twin is DTCM, and it had to be

The control is bit-exactly constant - one distinct cycle count over
25 000 traces - which is the strongest form it takes.

That is a consequence of a choice in `memory.ld` rather than luck. On an
M7 most RAM is behind the L1 D-cache: a table relocated to AXI SRAM at
`0x24000000` would still be read through a cache, and the twin would be
measuring a cache instead of being a control. DTCM is single-cycle and
not behind the D-cache, so it is the only true uncached twin the part
offers, and AXI SRAM is deliberately left out of the memory map so the
linker cannot fall into it.

## What is not here

* Only the reference cipher. Mbed-TLS and wolfSSL build for this part
  and are in `make matrix`, but have not been captured on it.
* Only two accelerator words. `0x307` (both caches) and `0x207`
  (D-cache only) are in `BOARD_ACR_WORDS` and were not captured. The
  both-caches cell is the interesting one, since the research repository
  records a 1 KB T-table giving up its key there while resident in a
  32 KB cache.
* The latency axis. Everything is at `LATENCY = 7`, the reset value,
  because the board refuses to go below it - the minimum this clock can
  run has not been measured and under-programming flash latency does not
  fault, it changes how the next fetch is served.
* Why four bytes resist at `acr 0x7`. The open question on this part.
* No off-chip arm. All of this is the victim timing itself.
