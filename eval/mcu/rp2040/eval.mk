# What is specific to MEASURING the RP2040, as opposed to running it.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 2 = 4 BYTES, AND THE DATASHEET SAYS 8. This is the one part in the tree
# where the documented figure and the measured one disagree, and it is the
# measured one that is here.
#
# RP2040 DS 2.6.3.2 describes the XIP cache as 16 KB with 8-byte lines, 2-way,
# 1024 sets. The research tree measured the fill granule directly - a stride
# walk over flash, counting hits with the part's own CTR_HIT/CTR_ACC - and got
# 75 % hits at stride 1, 50 % at stride 2 and 0 % AT STRIDE 4. A read at
# offset 4 after a read at offset 0 misses. The granule is four bytes, and
# the same sweep puts the perfectly-held capacity at 8 KB (2048 x 4 bytes),
# not 16 KB. See research/targets/rp2040.md sections 3a-3b; the roster row in
# research/reference/DATASHEETS.md carries the same number.
#
# WHAT GETTING THIS WRONG COSTS, because it does not degrade gracefully.
# EVAL_LINE_SHIFT is half of what turns a table's address into a row offset,
# and scoring a 4-byte row as an 8-byte one merges two real rows into one
# partition. The attack then pairs S-box indices that never share a flash
# read, tests a collision that does not exist, and returns a CLEAN NULL -
# which reads exactly like a part that does not leak. On this part the
# research tree's recovery runs at --line-shift 2 and the 8-byte model is the
# one that finds nothing.
#
# So the figure here is 4 bytes because that is what the silicon did, and the
# datasheet's 8 is recorded as wrong rather than averaged with it. How the two
# reconcile - a 4-byte fill granule in a structure the datasheet calls
# 8-byte-line - is still open (rp2040.md section 8); what is not open is which
# number the attack has to use.
EVAL_LINE_SHIFT = 2
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN and it
# is the thinnest here after the nRF52840's: one writable bit, plus a
# read-only witness. Read mcufw/rp2040/regs.h, not here, for why.
#
#   0x1  cache on. The reset state, and what the board boots into.
#   0x0  cache off - all four XIP aliases then bypass the cache and go
#        straight to the serial link.
#
# Bits 8..31 of the word that comes back are the low 24 bits of the CRC of
# the boot2 actually in flash, which is how a capture records the flash read
# configuration: 0x4eb274 for w25q080 and 0x4c22ea for generic_03h. A write
# ignores them.
#
# THERE IS NO LATENCY AXIS TO CROSS THESE WITH, and the axis that would be
# worth crossing is not a register at all. The cost of a miss is the serial
# link's clock divider, dummy cycles and sample delay, all programmed by
# boot2 before any application code runs, and the research tree measured
# 2.77x between the two variants mcufw/rp2040/board.mk ships. Sweeping it
# means relinking with BOOT2=, not writing a word.
EVAL_ACR_WORDS = 0x1 0x0
