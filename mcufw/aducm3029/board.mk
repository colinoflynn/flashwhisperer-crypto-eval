# DESC: Cortex-M3, 26 MHz, NO wait-state field at all - the knobless case
# ADuCM3029 (Analog Devices), EVAL-ADICUP3029.
#
# UNTESTED ON SILICON. This port compiles and links; nothing here has been
# flashed or captured from. See the note at the top of board.c.
#
# What makes it worth having is the absence: this part has no read-latency
# control anywhere in its memory map and no data cache, so it is the case
# where a defender has nothing to turn. The research tree recovers the key
# from it anyway, which is why "no wait states means no channel" is not a
# claim this project makes.

BOARD_CPU      = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# UART0 on P0_10/P0_11, which is where the board's DAPLink CDC port is.
# There is no second link: no other UART pinout has been established for
# this board, so asking for LINK=header is a make error rather than a build
# wired to nothing.
BOARD_LINKS    = vcp
# The accelerator words worth sweeping. ONE BIT WIDE, and the packing is in
# regs.h: bit 0 is FLCC_CACHE_SETUP.ICEN and there is nothing else on this
# part's read path to pack beside it.
#
#   0x1  instruction cache on. What ADI's own startup leaves, so what a
#        product ships, and what ssb_init boots into.
#   0x0  instruction cache off.
#
# Neither of these is a wait-state change, because this part has no wait
# states. A reader comparing a trace count here with an STM32 cell should
# know that.
BOARD_ACR_WORDS = 0x1 0x0
