# DESC: Cortex-M4, 16 MHz, 8-byte flash reads, ART with a 256 B D-cache
# STM32G474RE on a NUCLEO-64.
#
# Same FLASH_ACR interface as the two F4 parts, different everything
# else: RCC and FLASH move, GPIO is on AHB2, HSI16 is RCC_CR bit 8, and
# the USART has a kernel-clock select. See regs.h and board.c.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
BOARD_LINKS    = vcp
# DCEN is the axis on this part: the ART's I-cache and the prefetcher
# give no data reuse, the 256-byte D-cache does, and DCEN set is the
# CHIP DEFAULT. 0x500 against 0x100 is therefore the pair that matters,
# and 0x000 is the configuration the research tree measured bit-exact.
BOARD_ACR_WORDS = 0x500 0x100 0x700 0x300 0x000
