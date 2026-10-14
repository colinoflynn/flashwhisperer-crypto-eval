# DESC: Cortex-M7, 64 MHz, L1 I/D caches (FLASH_ACR has no accelerator bits)
# STM32H723ZG on a NUCLEO-144.
#
# The third part, and the one that stretches the contract rather than
# confirming it: the accelerator word is PACKED from three registers,
# because this part's FLASH_ACR holds LATENCY and WRHIGHFREQ and nothing
# else. See the header of regs.h for the packing and board.c for why.

BOARD_CPU      = -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the Nucleo-144 wires its virtual COM port to USART3 and
# breaks out no second UART by default.
BOARD_LINKS    = vcp
# LATENCY 7 (the reset value, and the floor board.c refuses to go below)
# with the four cache combinations. I-on/D-off is listed first because it
# is what production H7 firmware ships and it is the worst cell measured
# on this part: a 1 KB T-table gives up its key while resident in a 32 KB
# cache. Reading the packing off regs.h: 0x107 is I-on/D-off, 0x307 both
# on, 0x207 D-only, 0x007 the reset state with both off.
BOARD_ACR_WORDS = 0x107 0x307 0x207 0x007
