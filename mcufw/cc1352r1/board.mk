# DESC: Cortex-M4, 48 MHz, VIMS (cache / GPRAM / off) + a 16 B line buffer
# TI CC1352R1F3 on a LAUNCHXL-CC1352R1.
#
# The first TI part here and the first with no wait-state axis at all. Its
# accelerator word is PACKED and the packing is its own; read
# mcufw/cc1352r1/regs.h for it, not this file.
#
# NOT VERIFIED ON SILICON. The port compiles and its registers come from
# the rig that did run on this board, but nothing in mcufw/cc1352r1/ has
# been executed on a part. See board.c.
#
# -mfloat-abi=soft matches the rig. The die has an FPU, and the cipher
# does not use it; keeping the ABI the rig used means a difference in a
# cycle count is a difference in the flash path rather than in the
# calling convention.
BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the XDS110 Application UART on DIO13/DIO12 is the one link
# this board offers the victim. There is no second UART broken out, and
# the mailbox-over-cJTAG transport the rig also carries is not part of
# this tree's protocol.
BOARD_LINKS    = vcp
# MODE | PREF_EN | SYSBUS_LB_DIS | IDCODE_LB_DIS, packed per regs.h.
#
#   0x01  CACHE. What TI's own SDK startup leaves the part in, and it
#         leaks.
#   0x21  CACHE with the line buffer disabled. The negative control:
#         measured NOT to close the channel, same sigma as 0x01.
#   0x05  CACHE plus the tag prefetcher, which makes the channel
#         STRONGER rather than weaker.
#   0x03  OFF. Also leaks, and harder than cache mode.
#   0x23  OFF plus the line buffer disabled. The only configuration
#         measured bit-exactly constant on this part.
#   0x00  GPRAM, which is the boot state the ROM leaves. Measured
#         identical to OFF throughout.
#
# TIMING: the rig timed this part with DWT_CYCCNT and never used SysTick
# on it, so `TIMING=dwt` is the arm with evidence behind it. The SysTick
# arm checks at run time that its counter moves and reports 0 from
# ssb_has_cyc if it does not.
BOARD_ACR_WORDS = 0x01 0x21 0x05 0x03 0x23 0x00
