# What is specific to MEASURING the STM32L475VG.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 3 = 8 bytes, and it is MEASURED on this die rather than inferred from
# the family. The research tree's
# research/reference/DATASHEETS.md gives this part's read width as
# "64-bit (8 B), invariant across all 8 knob settings - a property of the
# array, not the accelerator", and records the data-side structure under
# it as a read latch of "1 x 8 B, depth 1".
#
# DO NOT USE THE CACHE PERIOD HERE, AND THE COST OF DOING SO IS MEASURED
# RATHER THAN ARGUED. The ART's D-cache on this part is 256 B, 8-way,
# with a 32-byte conflict period, so 5 is the plausible-looking wrong
# answer. The same roster records what it costs: scoring the 32-byte
# cache period instead of the 8-byte latch takes the byte S-box from
# 4 000 traces to 18 000, with 0/16 at 15 000 - a 4.5x penalty. So 5 is
# not a coarser version of 3, it is the setting that spends four times
# the traces, and a short ladder at it returns a clean null.
#
# (That 4.5x is itself a correction, which is worth knowing before
# quoting it: the roster withdrew an earlier "25x" because the 100 000
# in it was only the smallest rung that pass had TESTED at the 32-byte
# geometry, the rungs having been chosen for the 8-byte one.)
#
# The attack cell every recovery in the dossier is verified at is
# `--line-shift 3 --line-off 0`, with the S-box at 0x08002260 - offset 0
# in both an 8-byte and a 32-byte row, which is why that image can be
# scored both ways and compared.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = yes

# LATENCY | PRFTEN | ICEN | DCEN, which is the whole writable accelerator
# on this part. Readings below are the research tree's screen at 2 048
# traces (byte S-box, tables in flash, -O2, branchless xtime); the
# traces-to-key figures are from its ladder.
#
#   0x600  THE RESET DEFAULT, both caches on at 0 wait states: sd 2.461
#          over 18 distinct counts against a fixed-plaintext jitter of
#          0.245, and >275 000 traces to a key. It LEAKS, and it is what
#          ships - ST's factory image on this board runs 0x604.
#   0x004  LATENCY 4, whole ART off: sd 11.131 over 34 counts at jitter
#          0.000, and 4 000 traces. THE CHEAPEST CELL MEASURED ON THIS
#          PART. Turning the accelerator off is a >=68x discount for the
#          attacker against the reset default.
#   0x504  LATENCY 4, prefetch + D-cache: sd 22.416 over 114 counts - the
#          LARGEST on the whole surface - and 0/16 keys at 100 000. THIS
#          IS THE CELL TO RUN THE RECOVERY ON before believing any
#          ranking by standard deviation: sorted by sd this cell is top,
#          sorted by traces-to-key the order inverts.
#   0x404  LATENCY 4 + D-cache with the prefetch off: sd 21.220 over 57.
#          The pair with 0x504 is what separates the prefetch's
#          contribution from the cache's.
#   0x000  everything off at 0 wait states: 0.000, ONE distinct value.
#          The latch is still there and no register reaches it; it is
#          worth nothing when a miss costs no wait states. Same shape as
#          the L552's LATENCY 0 finding.
#
# TWO PREDICTIONS WORTH WRITING DOWN BEFORE THE CAPTURE, both measured on
# this die and both the opposite of the obvious:
#
#   * the >=68x discount above IS BUILD-DEPENDENT, not a property of the
#     part. It is >=68x on one of the two images measured and >=32x on
#     the other, because a 352-byte code addition moved the 0x004 break
#     from 4 000 to 12 500. A trace count here is under-specified without
#     the table's address.
#   * DCEN protects the byte S-box only because 256 B of cache holds
#     256 B of table, it does NOT protect the 4 KB 4-table, and its
#     protection is ALIGNMENT-dependent: with the code held fixed,
#     SBOX mod 32 = 0 is the one quiet build at 0x504 (sd 22.416)
#     against seven between 51.573 and 55.132.
EVAL_ACR_WORDS = 0x600 0x004 0x504 0x404 0x000
