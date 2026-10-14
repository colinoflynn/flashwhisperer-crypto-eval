# STM32G474RE - the narrow-read part, and the cache that is the channel

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

NUCLEO-G474RE, Cortex-M4 at 16 MHz (HSI, no PLL), SysTick, 25 000 traces
an arm unless stated. Every ciphertext checked against a reference AES
before anything was written.

This part reads its flash 64 bits at a time - 8 bytes, the narrowest of
the six here, against the F4s' 16 and the two M7s' 32 - so its byte
S-box wants `line_shift = 3`.

## The headline

With the ART data cache on and a compact T-table, this part gives up the
exact AES-128 master key at 250 000 traces. The vote reached 14/16
and the brute-force rescue finished the remaining two bytes.

Its ART data cache is 256 bytes.

## The accelerator sweep, byte S-box

| acr | what is on | mean | cycles sd | distinct | recovery |
|---|---|---:|---:|---:|---|
| `0x700` | prefetch + I-cache + D-cache | 5 148.46 | 2.4056 | 17 | 1/16 |
| `0x500` | prefetch + D-cache - the CHIP DEFAULT | 5 148.46 | 2.4056 | 17 | 1/16 |
| `0x300` | prefetch + I-cache | 5 142.00 | 0.0063 | 2 | 0/16 |
| `0x100` | prefetch | 5 142.00 | 0.0000 | 1 | control |
| `0x000` | nothing | 5 142.00 | 0.0000 | 1 | control |
| twin | tables in RAM | 5 182.00 | 0.0063 | 2 | control |

`0x700` and `0x500` are identical, so the I-cache does nothing on the
data path. `DCEN` is the whole axis, and `DCEN` set is the chip default.

## Clearing DCEN closes it, and costs nothing

With `DCEN` cleared the cipher is bit-exactly constant: a single
distinct cycle count over 25 000 traces. It is also 6 cycles FASTER than
with the cache on, because a 256-byte S-box exactly fills a 256-byte
cache and spends its time missing rather than hitting.

This agrees with the research tree, which records `LATENCY = 0` with the
D-cache off as bit-exact and as the correct setting at 16 MHz.

## The byte S-box does not fall; the compact T-table does

| table | cycles sd | recovery |
|---|---:|---|
| byte S-box, 256 B | 2.4056 | 1/16 at 25 000 |
| compact T-table, 1 KB | 2.0961 | exact key at 250 000 |

The 1 KB table overflows the 256-byte cache four times over and is the
one that breaks - while producing a SMALLER spread than the S-box that
fits. Screening on sigma would have thrown away the configuration that
yields the key.

The research tree's own recovery on this part was also a compact
T-table, at 20 000 traces. This one needed 250 000, so the two are not
directly comparable - a different clock, a different build and a
different instrument are all in play - but they agree on which table
shape is the vulnerable one.

## The ladder, and a correction worth keeping

```
 25 000    1/16
 50 000    9/16
100 000   13/16
175 000   14/16
250 000   14/16 + brute force  ->  EXACT MASTER KEY
```

That ladder was first run at the WRONG ROW WIDTH. Reasoning that a
compact T-table has 4-byte entries, the first attempt swept
`line_shift` 4, 2 and 1 - and 3, the correct width for this part, was
not among them. At `ls=4` the same 100 000-trace file reads 7/16 and
looks stuck; at `ls=3` it reads 13/16 and is plainly climbing.

The mistake was in the premise. The compact layout runs T-tables for
rounds 1..9 but a BYTE S-BOX final round, and the attack reads the last
round, so the entries are one byte and the width is the part's 8-byte
row: `ls=3`. Had the correction not been made, this part would be
written down here as a 7/16 partial.

## What is not here

* Only the reference cipher. Mbed-TLS and wolfSSL build for this
  part and are in `make matrix`, but have not been captured on it.
* The byte S-box was not pushed past 25 000 traces. It reads 1/16
  there, which is not "safe" - the compact table at the same accelerator
  word falls, and nothing says the S-box would not with more data.
* A disagreement with the roster, unresolved. It records tables in
  SRAM as the FASTEST placement on this part. Here the RAM twin is the
  slowest cell measured - 5 182 against 5 142 for flash with `DCEN`
  off. Most likely a difference of table shape or clock rather than a
  contradiction, but it is measured and it is not reconciled.
* No off-chip arm, and no latency axis.
