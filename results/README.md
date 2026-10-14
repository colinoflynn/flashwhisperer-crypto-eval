# Captures

One `.npz` per (part, configuration), under `results/<part>/`. **The data
itself is not tracked by git** — `git status` will not tell you a capture
is missing, and it is not meant to.

Each file holds `pt`, `ct`, `cyc` and a `meta` JSON blob, and the metadata
is **what the device reported**, not what the directory name implies. Those
have disagreed before: a build default flipped and a firmware image changed
meaning without changing name.

```python
import json, numpy as np
d = np.load("results/stm32l152re/refsbox-sbox-ram0-vcp-b115200-tsystick-w1-pad0.npz")
meta = json.loads(str(d["meta"]))
meta["table_addr"], meta["lib"], meta["cfg_str"], meta["acr"]
```

## Why the table address is in every file

**A trace count quoted without it is under-specified.** The same firmware
at three link-time accidents has needed 6 250, 12 500 and 25 000 traces for
the same recovery. The address in the file is the one the firmware read off
itself, which it knows better than a human copying it out of a `.map`.

An address of **0 means no table in flash**, which is a real answer: stock
Mbed-TLS generates its tables into `.bss` at run time and reports zero for
all three slots.

## What a capture has already been checked against

`tools/capture.py` refuses to write a file that fails any of these, so a
file existing means they passed:

* every ciphertext matches a reference AES under the victim's compiled-in
  key;
* the cycle column is not all zeros (a dead counter and a constant-time
  cipher are identical in every summary statistic, and nothing encrypts a
  block in zero cycles);
* if an accelerator word was requested, the device's **readback** matched
  it — a refusal aborts rather than filing the old setting under the new
  name.

## Reading a result

**A statistic is not a channel.** `sd` is not the answer; the answer is
whether `tools/attack.py` recovers the key and at how many traces. A large
spread can yield nothing, and a configuration that yields nothing is only
interesting once its RAM twin has shown the instrument was working.

Every flash-table arm here has a twin with the tables relocated to RAM.
Read them as a pair.
