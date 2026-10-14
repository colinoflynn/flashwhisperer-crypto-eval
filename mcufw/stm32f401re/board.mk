# DESC: Cortex-M4, 16 MHz, plain FLASH_ACR ART (LATENCY/PRFTEN/ICEN/DCEN)
# STM32F401RE on a NUCLEO-64.
#
# The small F4, and the first part here that has NOT been measured for
# this channel before - no roster row, no dossier. Its accelerator is the
# same register as the F429's, so the board file is short; what is
# unknown is the silicon, not the interface. See board.c.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the Nucleo-64 wires its VCP to USART2 and breaks out no
# second UART by default.
BOARD_LINKS    = vcp
# LATENCY | PRFTEN | ICEN | DCEN. At 16 MHz every wait-state value is
# legal, so the caches and the prefetch are the axis rather than the
# latency - the same shape as the F429's list, for the same reason.
BOARD_ACR_WORDS = 0x700 0x300 0x500 0x100 0x000
