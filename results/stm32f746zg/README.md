# STM32F746ZG - the hybrid, and the part where the D-cache is the fix

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

NUCLEO-F746ZG, Cortex-M7 at 16 MHz (HSI, no PLL), SysTick, the reference
byte-S-box cipher, 25 000 traces an arm. Every ciphertext checked against
a reference AES before anything was written.

This part is the hybrid of the other three. The L152 and F429 keep their
accelerator entirely in `FLASH_ACR`; the H723 has no accelerator bits
there at all and only the Cortex-M7 L1. This one has BOTH, so its opaque
word spans two peripherals and keeps `FLASH_ACR`'s own bit layout in the
low half - `0x307` here means what it means in ST's and wolfSSL's board
files, and the two L1 cache enables sit at bits 16 and 17.

## What was measured

| acr | what is on | mean | cycles sd | distinct | recovery |
|---|---|---:|---:|---:|---|
| `0x00000` | LATENCY 0, nothing else (reset) | 6 587.0 | 2.7298 | 21 | 1/16 |
| `0x00007` | LATENCY 7 | 12 108.9 | 14.9325 | 105 | 8/16 |
| `0x00307` | LATENCY 7 + prefetch + ART | 12 108.9 | 14.9325 | 105 | 8/16 |
| `0x10307` | the same + I-cache | 5 158.8 | 29.3506 | 208 | exact key at 5 000 |
| `0x20307` | the same + D-cache | 10 300.0 | 0.0885 | 2 | 0/16 |
| `0x30307` | both caches - THE VENDOR DEFAULT | 3 192.0 | 2.7296 | 21 | 1/16 |
| twin | tables in DTCM | 5 751.0 | 2.7341 | 11 | 0/16, control |

## The ART does nothing, measured to the digit

`0x00307` and `0x00007` differ by `PRFTEN | ARTEN` and are identical:
same mean, same sd, same min 12 039, same max 12 149, the same 105
distinct values, and the same 8/16 recovery. Not close - identical.

That is the predicted result rather than a surprise, and it is why the
bit is still in the word: the ART accelerates the ITCM flash alias and
this victim executes from the AXI alias, so it cannot reach the data
path. The research tree had measured that twice by other means. A
capture should record what was SET, not what somebody expected to
matter, and this is what "no effect" looks like when it is real.

## The D-cache closes it; the I-cache opens it

Those two bits are the whole story on this part, and they pull in
opposite directions:

* I-cache only is the worst cell on the part: the spread doubles to
  29.35 and the exact master key falls out at 5 000 traces.
* D-cache only very nearly closes the channel: sd 0.0885 over two
  distinct values, and 0/16.
* Both, which is ST's own default for this family, lands at 1/16.

The D-cache works here because the arithmetic is in its favour: a
256-byte S-box fits inside 4 KB, so it stops missing. That is a
statement about this table in this cache, not a general one - the H723
in this same tree has 32 KB of D-cache and the research tree records a
1 KB T-table leaking there anyway.

## The same pair of bits, the opposite conclusion from the H723

Both M7 parts here agree that I-cache-on with D-cache-off is the
dangerous cell, and both give up the key at 5 000 traces in it. They
disagree about everything else:

| | H723 | F746 |
|---|---|---|
| D-cache | 32 KB | 4 KB |
| D-cache on | not captured | closes it, 0/16 |
| vendor's own default | I on, D off - broken at 5 000 | both on - 1/16 |

So "the table fits in the cache" is not the predictor, and neither is
"this vendor's default is safe". Two parts from one vendor, one core
family, one cipher, measured on one bench with one instrument, and the
safe setting on one is the dangerous setting on the other.

## The twin is not bit-exact here, and that is expected

sd 2.7341 over eleven distinct values, where the L152's and H723's twins
are a single value. DTCM is not on the same fabric as the flash on this
part, which the research tree had already measured as a `flash - twin`
offset of +6.000 cycles.

A twin whose spread is not zero is still a control if its RECOVERY is
null, which is this project's own rule - a statistic is not a channel -
applied to the control rather than to the result. It recovers 0/16 under
every geometry at every trace count, so it holds.

## What is not here

* Only the reference cipher. Mbed-TLS and wolfSSL build for this
  part and are in `make matrix`, but have not been captured on it.
* The latency axis on its own. `0x00000` and `0x00007` bracket it
  and the rest of the sweep is at LATENCY 7, so wait states and caches
  are not separated.
* No off-chip arm.
* Why `0x30307` and `0x00000` both sit at 1/16 rather than 0/16 is
  not established; both are a hair above the twin.
