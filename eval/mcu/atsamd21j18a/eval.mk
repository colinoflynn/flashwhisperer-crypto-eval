# What is specific to MEASURING the ATSAMD21J18A, as opposed to running it.
#
# The split from mcufw/atsamd21j18a/ runs one way: anything that could CONTRADICT
# the board file belongs in the board file, because a board file and a build
# file disagreeing about the clock produces numbers rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device and not
# discoverable from it. Getting it wrong does not degrade gracefully: scoring
# an 8-byte row at the default of 4 merges two real rows into one partition,
# tests a collision that does not exist, and returns a clean null that reads
# exactly like a part that does not leak.
#
# 3 = 8 bytes.
EVAL_LINE_SHIFT = 3

# Measured, on this die, by the research tree rather than by this port.
# research/reference/DATASHEETS.md section A, "What the flash path is", gives
# the ATSAMD21J18A's flash read width as "64-bit (8 B)", with the data-side
# structure recorded in the same row as "a 64-byte direct-mapped cache - not
# a latch: two addresses 4 KB apart both stay resident", "8 lines x 8 B,
# 1 way, index A[5:3]". Section B's row for the part quotes every trace count
# at "--line-shift 3", which is the same number arrived at from the attack
# side.
#
# The value is therefore not this port's to establish, and this port has not
# re-measured it: the SAM D21 here has never been flashed.
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. The packing is this part's own, from
# mcufw/atsamd21j18a/regs.h: RWS in bits 0..3, READMODE in bits 8..9, CACHEDIS at
# bit 16. There is no prefetch bit and no separate instruction-cache enable
# on this part, so neither appears in the word.
#
#   0x00001  RWS 1, READMODE = NO_MISS_PENALTY, cache on. THE SHIPPED
#            CONFIGURATION at 48 MHz, and what the board boots into.
#   0x00101  READMODE = LOW_POWER. The vendor's own description is "inserts
#            a wait state each time there is a cache miss", i.e. a
#            documented timing channel, and the research tree measured it as
#            the worst cell on this part.
#   0x00201  READMODE = DETERMINISTIC. The vendor's own mitigation: "the
#            cache system ensures that a cache hit or miss takes the same
#            amount of time". Whether it holds is a key-recovery question,
#            not a sigma.
#   0x10001  CACHEDIS = 1, the other mitigation.
#   0x00000  RWS 0, which the board REFUSES at 48 MHz because the datasheet
#            allows it only to 24 MHz. The refusal is the point: the reply
#            is the readback, so a sweep records that it did not take.
#            Reaching it legally means the 8 MHz build point, which is
#            `CLK=8` on the make line. That is a build.mk axis: it puts
#            -DSS_CLK_MHZ=8 on the compile line and `-clk8` in the variant
#            string, so the 8 MHz sweep is a separate image under a
#            separate capture name. It was an edit to board.c until
#            build.mk grew the hook, and SS_CLK_MHZ=8 on the make line was
#            silently ignored before that; if a capture of this cell has no
#            `-clk8` in its name, it was taken at 48 MHz.
#            mcufw/atsamd21j18a/board.mk has the detail.
EVAL_ACR_WORDS = 0x00001 0x00101 0x00201 0x10001 0x00000
