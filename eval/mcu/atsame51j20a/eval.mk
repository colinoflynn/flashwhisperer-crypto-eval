# What is specific to MEASURING the Microchip ATSAME51J20A.
#
# The split from mcufw/atsame51j20a/ runs one way: anything that could
# CONTRADICT the board file belongs in the board file, because a board file
# and a build file disagreeing about the clock produces numbers rather than
# an error. So this holds only what the measurement needs and the board
# does not.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes, and it is MEASURED on silicon rather than inherited from
# anything. The roster in research/reference/DATASHEETS.md, section
# "Measured on this bench", gives this part's read width as "128-bit
# (16 B)" and records how it was got: the stride probe cannot see it,
# "10.0 cyc/read at every stride from 1 to 256", because at RWS 1 the loop
# is not memory bound, and "The pair probe is what answers" - 48 005 cycles
# below gap 16 against 56 005 flat from 16 to 4 096, which is one extra
# cycle per crossing read.
#
# The same roster's cipher row quotes the recovery it was used for: "S-box
# at 0x000022DC, line offset 12, --line-shift 4". Both structures on the
# read path hold 16-byte lines, the latch and the CMCC alike, so the two
# levels want the same number here.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN and
# it is documented in mcufw/atsame51j20a/regs.h, not here:
#
#   bits 0..3 RWS, bit 4 AUTOWS, bits 5/6 CACHEDIS0/1,
#   bit 8 CEN, bit 9 ICDIS, bit 10 DCDIS, bits 12..14 CSIZESW.
#
# Disable bits keep the hardware's polarity, so a 1 means OFF.
#
#   0x2010  AUTOWS, RWS 0, both line latches live, CMCC OFF, CSIZESW 4 KB.
#           THE RESET STATE, and on this part reset is NOT what a product
#           ships: the CMCC comes up disabled, so anything that enables it
#           is in a different and measurably safer configuration.
#   0x2110  the same with the CMCC ON at its full 4 KB. The research tree
#           has the 256-byte S-box and the 1.25 KB compact table both
#           bit-exactly constant here, and the 4 KB four-table the only
#           live cell, so this is where a null is the expected answer.
#   0x2510  CMCC on, DATA side disabled. One of the two cells the SAM D21's
#           largest open question asked for and could not have, because one
#           disable bit there killed both sides.
#   0x2310  CMCC on, INSTRUCTION side disabled. The other one.
#   0x0110  CMCC on, shrunk to 1 KB. The capacity axis as a register write
#           with the code, the link map and the wait state held fixed, and
#           the cell where the research tree saw the compact table go from
#           bit-exact to a live channel.
#   0x2001  AUTOWS cleared, RWS 1. The minimum this clock can run, and the
#           floor the board clamps to. Note bit 4 is CLEAR in these two: a
#           word that leaves AUTOWS set has its RWS field ignored by the
#           hardware, which would record a wait state that never ran.
#   0x2007  RWS 7. Wait states are a GAIN FACTOR on the measurement rather
#           than more information, which this part shows cleanly: chan_sd
#           on the four-table goes 14.931 / 26.072 / 46.041 at RWS 1 / 3 / 7
#           without the recovery getting any easier.
#   0x2070  everything off: CMCC disabled and both line latches disabled.
#
# 0x2070 MUST BE SWEPT LAST. Raising CACHEDIS0 is one-way on this die - the
# board refuses to lower it again because clearing it raises an imprecise
# bus error - so the sweep cannot return from this word without a reset.
# Anything after it measures a configuration that was refused.
EVAL_ACR_WORDS = 0x2010 0x2110 0x2510 0x2310 0x0110 0x2001 0x2007 0x2070
