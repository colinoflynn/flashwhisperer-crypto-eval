# DESC: Cortex-M4F, 8 MHz, EEFC read latch + CMCC cache, flash at 0x00400000
# Microchip ATSAMG55J19 on a SAM G55 Xplained Pro.
#
# Its flash is at 0x00400000, neither 0 nor 0x08000000, so a memory map copied
# from another port lands nowhere. EEFC_FMR.FWS is a PROGRAMMED insertion count
# rather than a clock-derived one, so it can legally be set above what the clock
# needs and swept as a gain factor at a fixed clock. UNTESTED ON SILICON - it compiles and nothing
# more; see the note at the top of board.c for which halves of it come from
# a rig that has been on the die and which come from vendor headers.

# -mfloat-abi=soft ON A PART THAT HAS AN FPU, deliberately, and taken from
# the research tree's rig so the two builds generate comparable code. The
# cipher uses no floating point, and a hard-float ABI needs SCB->CPACR
# enabled before main, which is one more thing to get wrong in bring-up for
# no measurement benefit. Soft float also keeps the generated code close to
# the other Cortex-M4 parts already measured.
BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the EDBG virtual COM port on USART7 (PA27/PA28) is the one
# serial path the board brings out without wiring something.
BOARD_LINKS    = vcp

# The packing is this part's own - FWS, SCOD, FAM and CLOE from EEFC_FMR in
# the low byte, the CMCC's enable, its two path disables and its
# programmable capacity above that. Read mcufw/atsamg55j19/regs.h, not this
# list, for the bit positions.
#
#   0x2040  FWS 0, CLOE, cache off, 4 KB capacity. THE VENDOR'S OWN WORD and
#           what the board boots into.
#   0x2140  the same with the cache enabled.
#   0x3140  the same again at the 8 KB capacity encoding.
#   0x2540  cache on with DCDIS set, the data path disabled behind it.
#   0x2046  FWS 6, cache off - the wait state as a gain factor at a fixed
#           clock, which is legal here because FWS is a programmed
#           insertion count rather than a clock-derived minimum.
#   0x2060  FAM set, which halves the flash read width. NOT comparable to
#           the others without changing the attack's row map with it.
BOARD_ACR_WORDS = 0x2040 0x2140 0x3140 0x2540 0x2046 0x2060
