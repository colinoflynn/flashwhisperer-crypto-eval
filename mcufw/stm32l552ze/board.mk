# DESC: Cortex-M33, 16 MHz, unified ICACHE + 2-entry read latch (packed word)
# STM32L552ZE on a NUCLEO-144 (NUCLEO-L552ZE-Q).
#
# The first Cortex-M33 part here, and the first whose accelerator is a
# separate peripheral rather than cache bits inside FLASH_ACR. The word is
# packed - LATENCY, ICACHE enable, ICACHE WAYSEL - with the packing taken
# unchanged from the research tree's rig so the labels mean the same thing
# in both sets of captures. See regs.h for the packing and board.c for
# what this port does and does not know.
#
# UNTESTED ON SILICON. It compiles; nothing more than that is claimed.

BOARD_CPU      = -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the board wires its virtual COM port to LPUART1 on PG7/PG8
# and the rigs break out no second link.
BOARD_LINKS    = vcp
# LATENCY in the low nibble, ICACHE enable at bit 8, WAYSEL at bit 9.
# 0x304 first because it is the state the part boots into here and the one
# the research tree's L552 measurements were taken in. eval.mk says what
# each cell is expected to show and why 0x000 is the interesting one.
BOARD_ACR_WORDS = 0x304 0x104 0x004 0x00F 0x000
