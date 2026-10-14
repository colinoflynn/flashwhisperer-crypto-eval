# ST STM32 Cryptographic Library — reserved, not wired up

This directory is a placeholder. The library is worth having here for one
reason the others cannot give: **it ships as a prebuilt binary archive**,
so it is the case that shows the table-residence control works without
source. That it works at all for a binary is only because ST compiled
`crypto.o` with `-fdata-sections`, which gives each table its own input
section — the same thing `instrumentation/victim/ld/sections.ld` relies on
everywhere else.

## Why it cannot be a submodule

**There is no upstream repository.** `STMicroelectronics/x-cube-cryptolib`
does not exist and STM32CubeL1 carries no crypto middleware; the library
ships inside X-CUBE-SBSFU. So it needs a fetch path and a SHA-256 manifest
rather than a gitlink — and it is ST's to distribute, not ours, so it is
fetched and verified rather than vendored.

## Three things that will cost a bench session if they are not known first

These are recorded from a working integration elsewhere, so that bringing
it up here is an afternoon rather than a week.

**The CRC peripheral must be clocked, and nothing in the API says so.** The
entire documentation of it is one line at the end of the release notes:
*"CRC pheripheral is used by STM32 cryptographic library."* Without the
clock the library does not fail — **it lies.** Every call returns success,
the key schedule is a FIPS-197-exact expansion, the output is
deterministic and varies with the plaintext, and it is simply the wrong
ciphertext. Ruled out first, each at the cost of a build-and-flash cycle:
endianness in a thousand combinations including state transposition,
buffer alignment, the decrypt core, double encryption, context reuse,
context overrun, and eight encodings of the key-size argument.

The only thing that catches it is the capture tool's check of the
ciphertext against a reference AES. A framing or length check passes, and
sixty thousand traces of a mis-driven cipher get recorded as a
measurement.

**The context needs headroom.** ST's header declares only `INCLUDE_AES128`,
so a translation unit including it sizes the expanded-key field at 44
words — but the ARCHIVE is the full library, built for all three key sizes
at 60, and it writes the schedule as if the field were that long. That is
64 bytes past the end of the context, over whatever follows it. It is
invisible in the obvious place: the field is last, so its OFFSET is
identical either way and dumping the first words gives exactly the right
key. Over-allocate and alias rather than editing ST's header.

**ST's context is CONSUMED by an operation.** Init-once/Append-many frames
correctly and returns a consistent but incorrect ciphertext for every
block. Use the documented one-shot Init/Append/Finish per block — which
is also what an integrator encrypting a single ECB block writes. Note that
this puts the key schedule INSIDE the measured window: a constant cost,
not a plaintext-dependent one, since the schedule's lookups are indexed by
the key, so it inflates the baseline and adds nothing the attack can use.
Worth saying out loud, because it makes ST's cycle counts not directly
comparable with libraries whose schedules run once at startup.

**And one build wart:** ST's `Common/types.h` re-typedefs `uint32_t` unless
it sees `_STDINT_H`, `__stdint_h` or `_STDINT`, none of which the GCC ARM
toolchain's `stdint.h` defines. Define `_STDINT_H` in *our* provider before
including ST's header — it is ST's own escape hatch, and it belongs in our
file rather than as a patch to theirs.

## What is also missing

`ST ships two archives per core`: the default and a time-optimised `_ot`
build whose text is roughly double. Both carry the same 256-byte S-box —
**ST's software AES is a byte S-box implementation with no T-tables**,
which the archive's symbol table shows directly. The device should report
which archive is linked by calling ST's own settings query and returning
its optimisation byte, so the answer comes from the library rather than
from a filename.
