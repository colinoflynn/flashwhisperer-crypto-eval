# What is specific to MEASURING the RP2350, as opposed to running it.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 3 = 8 BYTES, measured on silicon, and here the datasheet AGREES - which is
# worth saying out loud because on the RP2040 it does not. That part
# documents 16 KB of 8-byte lines and fills in FOUR, so the shift there is 2
# and this one's 3 cannot be inherited from it.
#
# The research tree measured it the same way on both: a stride walk over
# flash, counting hits with the part's own CTR_HIT/CTR_ACC. Stride 4 gives
# 128/256 hits - exactly half, each line read twice, miss then hit - and
# stride 8 gives 0/256. The tag granule comes out at 8 bytes too, from a
# working-set sweep at step 4 that pins at exactly 50.0 % for every working
# set at or above 16 KB, and addresses 8192 apart collide, which names 1024
# sets. 1024 x 2 ways x 8 bytes is the documented 16 KB. See
# research/targets/rp2350.md section 3; the roster row in
# research/reference/DATASHEETS.md carries the same numbers.
#
# WHAT GETTING THIS WRONG COSTS, because it does not degrade gracefully.
# EVAL_LINE_SHIFT is half of what turns a table's address into a row offset.
# Scoring an 8-byte row as a 4-byte one splits one real row into two
# partitions and tests a collision that is only half the truth; scoring it as
# 16 merges two rows. Either way the recovery returns a CLEAN NULL, which
# reads exactly like a part that does not leak - and on THIS part a null is
# the answer the project already has for a different reason, which is
# precisely why the number has to be right before anyone believes one.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN: CLKDIV
# in bits 0..7 and XIP_CTRL shifted left by 8 above it, so SPLIT_WAYS is bit
# 17 of the word and not bit 9. Read mcufw/rp2350/regs.h, not here.
#
#   0x00304  cache on, unsplit, CLKDIV 4 - the reset divider and the boot
#            state.
#   0x00004  cache off, CLKDIV 4.
#   0x00308  cache on, CLKDIV 8.
#   0x00310  cache on, CLKDIV 16.
#   0x20304  cache on with SPLIT_WAYS: one 2-way cache becomes two
#            direct-mapped half-caches.
#   0x00301  CLKDIV 1 - REFUSED by the board, and the refusal is the point.
#
# THIS IS THE ONE PART ON THIS BENCH WHERE THE GAIN FACTOR IS A LIVE
# REGISTER, and it is why the word carries CLKDIV at all. On the RP2040 the
# flash timing lives in boot2 and can only be varied by relinking, which is
# what left "does the dummy-cycle count behave like a wait state?" open in
# that part's dossier. Here it is one write, so the ledger's claim that wait
# states are a gain factor rather than information can be crossed against
# the cache enable on a part with no wait states at all.
#
# AND EXPECT THE STANDARD BUILD TO SHOW NOTHING UNTIL THE TABLE IS PROVED TO
# BE IN THE XIP WINDOW. The research tree's build on this part performs no
# flash access during the S-box encryption, which is why it is recorded as
# not broken rather than as safe. Check the address `make addr` prints
# against 0x10xxxxxx before reading a null here as a result.
EVAL_ACR_WORDS = 0x00304 0x00004 0x00308 0x00310 0x20304 0x00301
