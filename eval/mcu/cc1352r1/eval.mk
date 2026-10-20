# What is specific to MEASURING the TI CC1352R1F3, as opposed to running
# it. Anything that could CONTRADICT the board file belongs in the board
# file, because a board file and a build file disagreeing produces numbers
# rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes, and it is MEASURED on this part rather than inherited from
# another Cortex-M4. research/reference/DATASHEETS.md in the research tree
# gives this part's "flash read width" as "128-bit (16 B)" and its
# data-side structure as "flash line buffer + VIMS, which caches data"
# with depth "1 x 16 B behind an 8 KB array"; the roster row for the part
# repeats the width as "16 B (measured)" and the cache geometry as "8 KB =
# 512 lines x 16 B (measured)", both read off the die by a stride probe
# rather than taken from a datasheet.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = yes

# MODE | PREF_EN | SYSBUS_LB_DIS | IDCODE_LB_DIS. THE PACKING IS THIS
# PART'S OWN and lives in mcufw/cc1352r1/regs.h; there is no wait-state
# field in it because this part has no reachable wait-state axis.
#
#   0x01  CACHE. What TI's driverlib startup leaves the part in, so it is
#         the shipping configuration, and the byte S-box leaks there.
#   0x21  CACHE with IDCODE_LB_DIS set. THE NEGATIVE CONTROL and the
#         reason both knobs are in the word: the research tree measured
#         this cell identical to 0x01, so disabling the line buffer
#         WITHOUT also leaving cache mode changes nothing.
#   0x05  CACHE plus the tag prefetcher. Measured STRONGER than 0x01,
#         not weaker.
#   0x03  OFF. Measured worse than cache mode on the byte S-box: with
#         only a one-entry row register behind the array, adjacency
#         decides everything. "Turn the accelerator off" is not a fix
#         here, and this cell is where that is visible.
#   0x23  OFF plus IDCODE_LB_DIS. The one configuration measured
#         bit-exactly constant on this part.
#   0x00  GPRAM, which is what the ROM leaves at reset. Measured
#         identical to OFF in every column.
#
# Pair each with its RAM twin. On this part the twin matters more than
# usual, because the closing cell 0x23 is ALSO the slowest cell, so a
# constant column there has to be distinguished from a column that is
# constant because the cipher stopped reading flash.
EVAL_ACR_WORDS = 0x01 0x21 0x05 0x03 0x23 0x00

# The clock is a gain axis on this part rather than a wait-state field:
# PRCM_CPUCLKDIV at /2 was measured to make CACHE mode bit-exactly
# constant and to halve the amplitude in OFF mode. ssb_set_mhz accepts 48
# and 24 and refuses everything else. Nothing in this tree's protocol
# drives it yet, so it is recorded here as an axis that exists rather
# than as one that is swept.
