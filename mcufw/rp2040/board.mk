# DESC: Cortex-M0+, external QSPI flash via XIP, no wait-state field anywhere
# Raspberry Pi RP2040 on a Pico.
#
# UNTESTED ON SILICON - it compiles and has never been flashed. See board.c.
#
# The first part here with NO INTERNAL FLASH: code and constants are read in
# place out of an external QSPI device, over a serial link, through a 16 KB
# cache that instruction fetch and table reads share. There is no FLASH_ACR
# and no latency field, and the cost of a miss is set by the serial link -
# which boot2 programs before any of this code runs. See regs.h for the
# accelerator word and what it deliberately leaves out, and
# research/targets/rp2040.md in the research tree for the measurements.
#
# pyocd.target holds `rp2040`, which `python -m pyocd list --targets` resolves
# built in - no pack install, unlike the H723 and the F746.

BOARD_CPU      = -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
# THE SIXTH VARIABLE IS BOOT2 AND IT IS NOT IN docs/PORTING.md'S LIST OF
# FIVE, because no other part needs it. On this one the flash read
# configuration is not a register: the clock divider, the dummy cycles and
# the sample delay are all programmed by the second-stage loader, so WHICH
# boot2 is linked IS the flash configuration. The research tree measured
# 2.77x between the two shipped here.
#
#   w25q080      quad continuous read, the default for PICO_BOARD=pico - fast
#   generic_03h  plain 03h serial read, the universal path - slow
#
# TWO WAYS A SWEEP OF IT IS ATTRIBUTABLE, and they are independent. The
# build directory and the capture name carry the boot2, through
# BOARD_VARIANT below. And the device reports which boot2 is PHYSICALLY in
# flash, in the top 24 bits of the accelerator word (its CRC-32/MPEG-2), so
# a capture can be checked against the name it was filed under rather than
# trusted: 0x4eb274 is w25q080 and 0x4c22ea is generic_03h.
#
# The name half is new. Until build.mk read BOARD_VARIANT, two builds
# differing only in BOOT2 landed in the same object directory and the
# second overwrote the first, so any rp2040 capture taken before that and
# named without a `-b2...` component is only attributable the second way.
BOOT2         ?= w25q080
# READ BY build.mk, which composes VARIANT as
# $(LIB)$(LIB_VARIANT)$(BOARD_VARIANT)-ram... : a part gets the same right
# to name its own axis that a library already has with $(LIB_VARIANT). No
# other part sets it, so no other part's variant string moved when it
# started being read.
BOARD_VARIANT  = -b2$(BOOT2)
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/boot2_$(BOOT2).c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: UART0 on GP0/GP1. A Pico has no on-board bridge to the host, so
# this is the probe's CDC or an adapter on the header - which is all LINK=
# describes. There is no `header` option because there is no second console
# any rig in the research tree has brought up on this part.
BOARD_LINKS    = vcp
# The accelerator words worth sweeping, reading the packing off regs.h: the
# cache enable at bit 0, and a READ-ONLY witness of the boot2 checksum in bits
# 8..31 which a write ignores.
#
#   0x1  cache on. The reset state, and what the board boots into.
#   0x0  cache off. DS 2.6.3.1: with EN = 0 all four XIP aliases bypass the
#        cache and go straight to the serial link, so this is the cache
#        on/off axis and the only register one this part has.
#
# Two words rather than the five an ST part sweeps, because there is no
# latency to cross them with. The axis that would be worth crossing - the
# serial link's own timing - is BOOT2 above, and it is not a register.
BOARD_ACR_WORDS = 0x1 0x0
