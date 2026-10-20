# What is specific to MEASURING the ATSAMG55J19.
#
# The split from mcufw/atsamg55j19/ runs the usual way: anything that could
# CONTRADICT the board file belongs in the board file, because a board file
# and a build file disagreeing produces numbers rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes, measured on silicon by the research tree rather than taken
# from a datasheet row. research/reference/DATASHEETS.md, the roster row for
# ATSAMG55J19, gives the read width as "128-bit (16 B) at reset", with the
# fit quoted as "stride fit 16 B vs 8 B at residual 0.0000 with base and
# penalty identical; pair step at gap 16 vs gap 8", and the same roster's
# recovery cell records the attack that used it: "S-box at 0x00401a1c, line
# offset 12, --line-shift 4", verified by inverting k10 to the master key
# and re-encrypting 4 096 captured plaintexts.
#
# SO THIS IS MEASURED - BUT ONLY FOR ONE VALUE OF ONE CONFIGURATION BIT,
# AND THAT BIT IS IN THE ACCELERATOR WORD. The same roster row records the
# width as "a SOFTWARE SETTING - EEFC_FMR.FAM = 1 measures as 8 B", the only
# software-settable read width on that bench. FAM is bit 5 of this part's
# packed word, so acr 0x2060 and anything else with that bit set needs
# --line-shift 3, not 4, and scoring it at 4 merges two real rows into one
# partition and returns a clean null. Pass both line shifts on any sweep
# that moves FAM.
#
# Note also that the address above is the research tree's own rig, not this
# repository's build. A trace count or a row map quoted without the table's
# address is under-specified, so read the address off `make addr` for the
# image actually flashed rather than reusing 0x00401a1c.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. The packing is this part's own, and
# it spans TWO peripherals: FWS / SCOD / FAM / CLOE from EEFC_FMR, then the
# CMCC's enable, its two path disables and its programmable capacity. See
# mcufw/atsamg55j19/regs.h for the bit positions; do not infer them from the
# hex here.
#
#   0x2040  FWS 0, CLOE on, cache OFF, PRGCSIZE 2 (4 KB). THE SHIPPED WORD -
#           the vendor's system_samg55.c sets CLOE at every frequency and
#           never enables the cache - and the research tree measured it as
#           the leakiest of the eight SCOD/FAM/CLOE corners.
#   0x2140  the same with the cache ON. On that bench this was a complete
#           fix for the byte S-box and the compact 1-table and an amplifier
#           for the 4-table, so it is not a one-directional knob.
#   0x3140  cache on at the 8 KB capacity encoding, which was the best fix
#           measured there and costs nothing, a larger cache being faster.
#   0x2540  cache on with DCDIS set. Measured as the WORST cell on that
#           part, worse than the cache being off entirely.
#   0x2046  FWS 6, cache off. The wait state is a programmed insertion count
#           here rather than a clock-derived minimum, so it can be set above
#           what the clock needs and swept as a gain factor at a FIXED
#           clock, table address and link map.
#   0x2060  FAM set, the read width halved. SCORE THIS AT --line-shift 3.
#
# Those characterisations are from the research tree's own rig at its own
# clock and its own table addresses. They say what is worth sweeping here;
# they are not predictions about what this repository's build will measure,
# and this port has not been on silicon at all.
EVAL_ACR_WORDS = 0x2040 0x2140 0x3140 0x2540 0x2046 0x2060
