# What is specific to MEASURING the STM32H723ZG, as opposed to running it.
#
# Same rule as the other parts: anything that could CONTRADICT the board
# file belongs in the board file, because a board file and a build file
# disagreeing about the clock produces numbers rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 5 = 32 bytes. This part reads its flash 256 bits wide, measured on
# silicon rather than taken from a datasheet row, and it is the widest of
# the three parts here - the L152 is 8 bytes and the F429 16.
#
# So the byte S-box wants --line-shifts 5 and a 4-byte word table wants 3,
# and getting it wrong does not degrade gracefully: it returns a clean
# null that reads exactly like a part which does not leak. Pass both.
EVAL_LINE_SHIFT = 5

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN -
# FLASH_ACR here holds LATENCY and WRHIGHFREQ and no accelerator bits at
# all, so mcufw/stm32h723zg/regs.h packs the two L1 cache enables and the
# ITCM-side ART alongside the latency. Read it there, not here.
#
#   0x107  LATENCY 7, I-cache ON, D-cache off. What production H7 firmware
#          ships, and the worst cell measured on this part.
#   0x307  both L1 caches on. A 1 KB T-table has given up its key here
#          WHILE RESIDENT in a 32 KB cache, which is why "the table fits
#          in the cache" is not a constant-time argument.
#   0x207  D-cache only.
#   0x007  the reset state: seven wait states, both caches off. Unlike the
#          F4/G4, this part boots slow AND uncached.
#
# LATENCY is held at 7 throughout, deliberately. The board refuses to go
# below the reset value because the minimum this clock can run has not
# been measured, and under-programming flash latency does not fault - it
# changes how the next fetch is served.
EVAL_ACR_WORDS = 0x107 0x307 0x207 0x007
