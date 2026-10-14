# STM32F401RE - a part nobody had measured, and it falls

*🤖WARNING🤖: This file LLM generated and may read oddly. Will eventually be human-edited for that real-life touch and typos.*

NUCLEO-F401RE, Cortex-M4 at 16 MHz (HSI, no PLL), SysTick, 25 000 traces
an arm unless stated. Every ciphertext checked against a reference AES
before anything was written.

This is the first part in this tree that had never been measured for this
channel - no roster row and no dossier anywhere in the research
repository, only an unrelated constant-time rig. So nothing here is a
reproduction, and the flash read width had to be established rather than
inherited from the F429.

## The headline

With the ART data cache on and a compact T-table, this part gives up the
exact AES-128 master key at 100 000 traces, verified against the
victim's compiled-in key and against a known plaintext/ciphertext pair.

Its ART data cache is 128 bytes.

## The accelerator sweep, byte S-box

| acr | what is on | mean | cycles sd | distinct | recovery |
|---|---|---:|---:|---:|---|
| `0x700` | prefetch + I-cache + D-cache | 4 834.87 | 3.8773 | 32 | 2/16 |
| `0x500` | prefetch + D-cache | 4 834.87 | 3.8773 | 32 | - |
| `0x300` | prefetch + I-cache | 4 815.00 | 0.0063 | 2 | - |
| `0x100` | prefetch | 4 815.00 | 0.0000 | 1 | control |
| `0x000` | nothing | 4 815.00 | 0.0000 | 1 | control |
| twin | tables in RAM | 4 815.00 | 0.0063 | 2 | control |

`0x700` and `0x500` are identical to four decimals, so the I-cache
contributes nothing on the data path. That is the F429's roster claim -
"DCEN only; ICEN and PRFTEN give no data reuse" - reproduced on a part
it was never measured on.

## Clearing DCEN closes it, and costs nothing

With `DCEN` cleared the cipher is bit-exactly constant: one distinct
cycle count over 25 000 traces, landing on the same 4 815.000 as the RAM
twin. It is also 20 cycles FASTER than with the cache on.

The arithmetic says why. The ART data cache here is 128 bytes and a byte
S-box is 256, so the table cannot fit and the cache spends its time
missing. Turning it off removes the misses and the channel together.
There is no performance argument for leaving it on.

## The table has to overflow the cache - but the SPREAD says the opposite

Same cipher, same part, same accelerator word, a 1 KB compact T-table
instead of the 256-byte S-box:

| table | cycles sd | recovery |
|---|---:|---|
| byte S-box, 256 B | 3.8773 | 2/16 at 25 000 |
| compact T-table, 1 KB | 2.5687 | exact key at 100 000 |

The bigger table produces a SMALLER spread and is the one that breaks.
Screening these cells on sigma - the obvious first instinct - would have
discarded the only configuration that yields a key. A statistic is not a
channel, in the direction that costs you the result rather than the one
that gives you a false positive.

## It was the trace count, and the ladder says so

```
 25 000    6/16
 50 000    2/16
100 000   16/16   EXACT MASTER KEY
```

A partial that climbs is a data problem, and this one climbed. The dip at
50 000 is a reminder that a single rung is not a trend.

## The row width is 4, and this is how it was established

16 bytes, and MEASURED on this part rather than copied from the F429.
The evidence is the recovery itself: at `line_shift = 4` the exact key
falls out at 100 000 traces, where on the same capture `ls=2` reaches
7/16 and `ls=1` only 3/16.

Note what the 4 counts. The compact layout runs T-tables for rounds 1..9
and a BYTE S-BOX final round, and the attack reads the last round - so
16 entries per row means 16 one-byte entries, a 16-byte read. It is not
16 four-byte entries. Getting that backwards is how the G474's first
ladder here was run at the wrong width and reported 7/16 where the right
width gives 13/16 on the same file.

## What is not here

* Only the reference cipher. Mbed-TLS and wolfSSL build for this part
  and are in `make matrix`, but have not been captured on it.
* The byte S-box was not pushed past 25 000 traces. It reads 2/16
  there. Whether it also falls with more data is untested, and "2/16 at
  25 000" is not "safe".
* No off-chip arm, and no latency axis - everything is at 16 MHz
  where every wait-state value is legal.
