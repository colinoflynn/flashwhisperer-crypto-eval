# DESC: Renesas Synergy S5D3, Cortex-M4, 120 MHz, FCACHE + FLWT packed (UNTESTED)
# R7FS5D37A3A01CFP.
#
# Its accelerator is one cache enable bit that serves instruction fetch and
# data reads alike, which no ST part here does, plus a
# three-bit wait-state field in a different register, so the one opaque word
# the contract wants is PACKED. The packing is in regs.h.
#
# THE PORT HAS NOT BEEN RUN ON SILICON, and three pieces of it are not
# established: the console, the trigger pin and the boot block below
# 0x00000500. board.c and memory.ld say which is which at the point it
# matters.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# NEITHER NAME IS ACCURATE HERE. The rig this port comes from drives the
# part over SWD through a mailbox in SRAM and defines no UART, so there is no
# S5D3 serial register in the source of truth and none is invented. board.c
# carries the mailbox instead. build.mk admits only `vcp` and `header`, and
# adding a third name would mean editing a file outside this directory, so
# `header` is the token rather than a claim. Nothing in tools/ drives this
# link yet.
BOARD_LINKS    = header
# FLWT[2:0] | FCACHEEN<<8, as packed in regs.h.
#
#   0x102  FCACHEEN on, two wait states. THE CONFIGURATION THE PART SHIPS IN
#          at 120 MHz, and the one the research tree measured leaking.
#   0x002  the same wait states with the cache off, which is the fix that
#          costs speed. One bit covers both sides of the flash path here,
#          so this is not an ST-style cache-only arm.
#   0x104  four wait states, cache on. Wait states are expected to scale the
#          channel rather than carry it, which is a claim to check on this
#          part and not an assumption to build in.
#   0x107  seven, the maximum the field holds.
#
# FLWT below 2 is refused by board.c at 120 MHz, so it is not listed. That is
# not caution: driving FLWT to 0 at 120 MHz wedged the core on the rig.
BOARD_ACR_WORDS = 0x102 0x002 0x104 0x107
