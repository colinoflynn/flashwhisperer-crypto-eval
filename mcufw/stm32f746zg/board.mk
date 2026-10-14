# DESC: Cortex-M7, 16 MHz, FLASH_ACR with ART *and* L1 I/D caches
# STM32F746ZG on a NUCLEO-144.
#
# The hybrid: a real FLASH_ACR in the F4 shape (PRFTEN, ARTEN) AND the
# Cortex-M7's two L1 caches in the SCB. Its accelerator word spans both
# and keeps FLASH_ACR's own bit layout in the low half, so 0x307 here
# means what it means in ST's and wolfSSL's board files. See regs.h.

BOARD_CPU      = -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the Nucleo-144 wires its VCP to USART3 and breaks out no
# second UART by default.
BOARD_LINKS    = vcp
# 0x30307 is the vendor's own configuration for this family - LATENCY 7 |
# PRFTEN | ARTEN with both L1 caches on, which is what
# wolfssl-examples-stm32's boards/f767/hw_init.c writes. The rows bracket
# it: caches off, I only, D only, and the bare register with no ART.
BOARD_ACR_WORDS = 0x30307 0x00307 0x10307 0x20307 0x00007
