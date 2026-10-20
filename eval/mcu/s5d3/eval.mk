# What is specific to MEASURING the Renesas Synergy S5D3, as opposed to
# running it. Anything that could CONTRADICT the board file belongs in the
# board file, because the two disagreeing about a clock produces numbers
# rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes, and it is measured on this part rather than inherited from
# the other Renesas here. research/reference/DATASHEETS.md in the research
# tree, section A, row "Synergy S5D3 (R7FS5D37A3A01CFP)", gives the flash
# read width as "128-bit (16 B)" with the data-side structure "a single row
# register, and FCACHEEN reaches it" at a depth times granularity of
# "1 x 16 B".
#
# The measurement behind that row is a forward stride walk with the SRAM
# twin subtracted, at FCACHEEN = 1, FLWT = 2, 120 MHz: the cost per read is
# exactly 2 x stride / 16 up to stride 16 and flat at 2.000 cycles from
# stride 16 through 256, which is one miss per 16 bytes. The latch boundary
# measured separately agrees with it.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = yes

# FLWT[2:0] | FCACHEEN<<8. THE PACKING IS THIS PART'S OWN: the cache enable
# and the wait states are in different registers at different widths, so
# mcufw/s5d3/regs.h packs them and ssb_acr_get reassembles the word from
# both. Read the packing there, not here.
#
#   0x102  FCACHEEN on, FLWT 2. What a 120 MHz build runs, and the cell to
#          beat.
#   0x002  cache off at the same wait states. One enable covers instruction
#          fetch and data reads on this part, so this arm is not the
#          ST-style "data cache off" cell and should not be read as one.
#   0x104  four wait states with the cache on.
#   0x107  seven, the top of the field.
#
# FLWT 0 and 1 are absent because board.c refuses them at 120 MHz. Driving
# FLWT to 0 at that clock wedged the core on the rig, so the floor is a
# measured limit rather than a datasheet one.
EVAL_ACR_WORDS = 0x102 0x002 0x104 0x107
