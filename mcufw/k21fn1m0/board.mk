# DESC: Cortex-M4F soft-float, ~21 MHz, FMC packed word (5 enables x 2 banks + OUTDIV4)
# NXP Kinetis MK21FN1M0VMC12 on a TWR-K21F120M.
#
# The first NXP Kinetis here, and UNTESTED ON SILICON - see board.c. Its
# accelerator is two FMC control registers rather than one FLASH_ACR, with
# five separately switchable enables each over two independent flash
# blocks, so the opaque word is packed; regs.h documents the packing.
#
# Built soft-float although the core has an FPU. That is the rig's choice
# and the reason is the measurement: the cipher is integer, and enabling
# the FPU would add a lazy-stacking term inside the timed region.
#
# WHAT pyocd.target HOLDS, AND WHY IT IS NOT ENOUGH. That file carries the
# name on one line and nothing else, because tools/capture.py reads it
# whole; the notes that would otherwise sit in it are here instead.
#
# `python -m pyocd list --targets | grep -i k21` returns nothing. The NXP
# built-ins stop at k20d50m, k22f, k22fa12, k28f15, k64f and k66f18. The
# name in pyocd.target is a CMSIS pack target, found with `python -m pyocd
# pack find k21`, which lists MK21FN1M0VMC12 in NXP.MK21F12_DFP 10.0.1 and
# reports it not installed here. `python -m pyocd pack install
# mk21fn1m0vmc12` fetches it. Whether pyocd can then program this die has
# NOT been established - nothing has been flashed from this port.
#
# The probe is the harder half. The TWR-K21F120M's on-board debug circuit
# is OSBDM/OSJTAG, vendor-class on both USB interfaces, with no driver here
# and a discontinued vendor toolkit, and pyocd cannot drive OSBDM at all.
# rigs/fw_k21_aes flashes this part with a SEGGER J-Link over SWD on J1,
# the 20-pin 0.05 inch Cortex Debug header, and so would this port.
#
# AND THE WARNING THAT IS NOT RECOVERABLE: connecting a J-Link to a SECURED
# Kinetis mass-erases it inside `connect`, with no prompt, announced only
# by one line in the connect log. startup.c emits a flash configuration
# field with FSEC = 0xFE so that images built here leave the part
# unsecured; an image built WITHOUT that field re-secures it at every
# reset. memory.ld asserts the field is present, so a build that lost it
# fails to link rather than locking a board.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# `header` is a misnomer here and board.c says so at length. This board has
# no usable serial port at all - its only USB-serial path is the OSJTAG
# vendor-class bridge, which has no driver - so the console is a ring
# buffer in SRAM for a debugger to drive, as the rig's mailbox is. Of the
# two names build.mk accepts, "not the virtual COM port" is the less wrong.
BOARD_LINKS    = header
# Packed word: enables | CRC<<5 | CLCK_WAY<<8 | (OUTDIV4-1)<<12. Bits 21..16
# are the B0RWSC and B0MW readbacks and are ignored on a set.
#
# All five entries keep OUTDIV4 at 2, the measured reset divider, so the
# wait state stays where it was and these five cells differ only in which
# parts of the accelerator are on. The wait-state axis is a separate sweep
# on the same word, and on this part it is driven by that divider rather
# than by a latency field.
#
#   0x101F  all five enables on, LRU, no lock - the reset state
#   0x1007  SEBE + both prefetches, caches off
#   0x1006  caches off AND the single entry buffer off
#   0x1001  single entry buffer only
#   0x1000  everything off
BOARD_ACR_WORDS = 0x101F 0x1007 0x1006 0x1001 0x1000
