# What is specific to MEASURING the ADuCM3029, as opposed to running it.
#
# Same rule as the other parts: anything that could CONTRADICT the board
# file belongs in the board file, because a board file and a build file
# disagreeing about the clock produces numbers rather than an error.
#
# The port itself is UNTESTED ON SILICON (see mcufw/aducm3029/board.c). The
# two numbers below are not: they come from the research tree's own
# measurements on this part, and they are the reason a port exists at all.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 3 = 8 bytes. research/reference/DATASHEETS.md in the research tree gives
# this part's flash read width as, quoting the cell verbatim,
#
#     8 B - read off the attack, not off a probe
#
# and in the same table's second half, "none - no wait-state field
# anywhere" with "ICEN is the only read-path bit, and it carries nothing".
#
# READ WHAT THAT QUALIFIER MEANS BEFORE USING THE NUMBER. Three direct
# geometry probes on this part find no address dependence at all, so the
# width was not read off a stride or pair walk the way the G474's and the
# F746's were. It was read off a line-shift sweep of the attack, and the
# dossier (research/targets/aducm3029.md section 3) records the sweep and
# its asymmetry: shift 2 (4 B) gives 0/16 key bytes at every trace count,
# shift 3 (8 B) gives 16/16 at 2 500, shift 4 (16 B) gives 11/16 at 2 500
# and 16/16 at 20 000. A model too FINE splits real rows and destroys the
# collision structure; a model too COARSE merely merges two rows and
# dilutes it. That asymmetry is what makes 8 B the answer rather than a
# best fit, and the dossier states it in those terms: "The read width is a
# measurement, not the ECC inference."
#
# So this is measured, by a verified key recovery, which is the same
# evidence that sets MEASURED = yes on the F401RE. It is NOT measured by a
# read-width probe, and the DATASHEETS cell says so on purpose. If a reader
# needs a probe-established width, treat this as unestablished and read the
# dossier's section 3 before quoting it.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN and
# it is one bit: there is no FLASH_ACR here, no wait-state field anywhere
# in the memory map, and no data cache. Read mcufw/aducm3029/regs.h, not
# this file, for why.
#
#   0x1  FLCC_CACHE_SETUP.ICEN set. The instruction cache on, which is what
#        ADI's own system_ADuCM3029.c leaves and so what a product ships.
#   0x0  the same bit clear.
#
# EXPECT NO DIFFERENCE IN THE DATA PATH, and that is the result rather than
# a broken axis: the cache this bit gates is instruction-only, the research
# tree measured that it does not carry the channel, and the part leaks in
# every flash configuration because there is no read-path control to turn.
# The useful control on this part is RAMTAB=1, not an accelerator word.
EVAL_ACR_WORDS = 0x1 0x0
