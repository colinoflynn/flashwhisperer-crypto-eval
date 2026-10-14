# DESC: Cortex-M4F, 16 MHz, 4-bit LATENCY + ART I/D caches, DWT or SysTick
# STM32F429ZI on a NUCLEO-144.
#
# The second part ported, chosen because it differs from the first in all
# three places the board contract has hooks for: a four-bit wait-state
# field with two resettable caches, a cycle counter that counts UP, and a
# different console. See the header of board.c.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`. The Nucleo-144 wires its virtual COM port to USART3 and does
# not break out a second UART by default, so there is no `header` link to
# offer - and asking for one is a make error rather than an image wired to
# nothing.
BOARD_LINKS    = vcp
# LATENCY | PRFTEN | ICEN | DCEN, and the interesting subsets. At 16 MHz
# every wait-state value is legal, so the caches and the prefetch are the
# axis rather than the latency.
BOARD_ACR_WORDS = 0x700 0x300 0x500 0x100 0x000
