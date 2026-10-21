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

# THE CELL THIS PART CANNOT HOLD, and the measured reason.
#
# LibTomCrypt's default configuration has twelve 1 KB tables that survive
# garbage collection - TE0..TE3, the four last-round Te4_*, and Tks0..Tks3,
# which rijndael_setup references while building the decrypt schedule. The
# RAMTAB=1 twin puts all 12 KB in SRAM, and 16 KB of SRAM less ADI's reserved 0x200 leaves 2 560 bytes
# above .bss where instrumentation/victim/ld/sections.ld asserts 4 096:
# short by 1 536. Measured, not estimated, with the key schedule already
# reduced to its 508-byte Rijndael size (see eval/lib/libtomcrypt/
# provider.c); before that it overflowed the region outright by 1 192.
#
# NOT FIXED BY LOWERING THE ASSERTION. 2 560 bytes may well be enough stack
# for this victim, but nothing here has measured its worst case, and this
# part has never been flashed - inventing a headroom figure that makes the
# link pass is the one move that would make the number meaningless.
#
# `make matrix` builds the LTC_SMALL_CODE twin instead, which has three
# 1 KB tables and links with 11 776 bytes of headroom, so the library still
# has a residence control on this part rather than none.
BOARD_NO_CELLS = libtomcrypt-few0-ram1
