# DESC: Cortex-M3, 32 MHz, one-bit LATENCY, no cache - the floor case
# STM32L152RE.
#
# The floor case, and the reference part: a measured baseline exists for it,
# so `LIB=refsbox` here must reproduce that before any library result on any
# part is worth reading.
#
# A port supplies exactly these five variables plus its sources. Nothing
# else in the build knows what part it is building for.

BOARD_CPU      = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# The link this part's victim speaks on. `header` is USART1 on the Arduino
# pins, for the off-chip instrument; `vcp` is USART2 on the ST-LINK's virtual
# COM port, which needs no wiring and is the bring-up path.
BOARD_LINKS    = header vcp
# Accelerator words worth sweeping, and what each means. The host records the
# word; this list is what `make matrix` walks.
BOARD_ACR_WORDS = 0x07 0x05 0x06
