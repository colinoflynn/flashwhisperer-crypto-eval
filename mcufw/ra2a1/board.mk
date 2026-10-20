# DESC: Cortex-M23, 48 MHz, FCACHE + MEMWAIT + FLWT packed - UNTESTED port
# Renesas RA2A1 (R7FA2A1AB3CFM).
#
# A Cortex-M23 whose wait states are NOT in a flash register: SYSTEM.MEMWAIT
# and FLWT live apart. Its accelerator is not one register and not in one
# peripheral, so the opaque word is packed; the
# packing is in mcufw/ra2a1/regs.h and nowhere else.
#
# COMPILED ONLY. No board was attached and nothing has been flashed. board.c
# says what that means and which three things it could not establish.

BOARD_CPU      = -mcpu=cortex-m23 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# `vcp` is the only value build.mk accepts that fits, and on this part it does
# NOT mean a virtual COM port: the board's J-Link OB enumerates with no CDC
# interface, so the host link is the debug probe. board.c implements it as a
# RAM mailbox and says what is missing on the host side.
BOARD_LINKS    = vcp
# FCACHEEN (bit 0) | MEMWAIT (bit 4) | FLWT (bits 8..10).
#
#   0x011  cache on, one memory wait, FLWT 0. THE CONFIGURATION THE PART
#          SHIPS IN, created by firmware rather than by reset, and the one
#          that gave up the full key at 200 000 traces.
#   0x010  the same with the cache off. The off switch, and on this part it
#          reaches the whole structure.
#   0x711  cache on, FLWT at its maximum. Measured on the die at stride 64,
#          that is 16.160 cycles a read against 10.070 at FLWT 0, so about
#          six cycles dearer with the cache still absorbing most of it.
#   0x710  the same with the cache off, where the field bites hardest:
#          43.203 against 16.090 cycles a read, about 27 cycles dearer.
#   0x001  MEMWAIT 0, which is prohibited above 32 MHz. REFUSED by the board,
#          and the refusal is the point: SS_V_SET_ACR replies with the
#          readback so a sweep records that it did not take.
BOARD_ACR_WORDS = 0x011 0x010 0x711 0x710 0x001
