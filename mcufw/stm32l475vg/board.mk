# DESC: Cortex-M4F, 16 MHz, 8-byte flash reads, ART with a 256 B D-cache
# STM32L475VG on a B-L475E-IOT01A (the IoT Discovery node), NOT a Nucleo.
#
# UNTESTED ON SILICON: written from rigs/fw_l4_aes/ in the research
# tree, built, never run. See board.c for what that leaves open.
#
# Same FLASH_ACR interface as the G474 and the two F4 parts, so an ACR
# value here means what it means there. What moves: the part boots on MSI
# at 4 MHz with HSI16 OFF, the console is USART1 on GPIOB rather than
# USART2 on GPIOA, LATENCY is a THREE-bit field, and the board is a
# Discovery kit whose pinout is not a Nucleo's. See regs.h and board.c.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: this board's ST-LINK VCP is USART1 on PB6/PB7, measured off
# the live factory image rather than read out of UM2153. No second UART is
# offered because none has been established on this board the same way -
# `header` would be a guess, and a link wired to nothing builds fine.
BOARD_LINKS    = vcp
# LATENCY | PRFTEN | ICEN | DCEN, every one of these a cell the research
# tree has measured on this die. Ordered so the first two are the pair
# that makes this part's point:
#
#   0x600  THE RESET DEFAULT - both caches on, prefetch off, 0 wait
#          states - and it LEAKS (sd 2.461 over 18 distinct counts). ST's
#          own factory demo on this board runs 0x604. It is also the
#          most expensive cell measured here: >275 000 traces.
#   0x004  LATENCY 4, whole ART off. The CHEAPEST cell on this part at
#          4 000 traces, which makes switching the accelerator off a
#          discount for the attacker rather than a fix.
#   0x504  LATENCY 4, prefetch + D-cache. The LARGEST standard deviation
#          of the whole surface (22.416) and 0/16 keys at 100 000. In the
#          list precisely because a statistic is not a channel.
#   0x404  LATENCY 4 + D-cache, prefetch off: 0x504 with the one bit
#          changed, so the pair separates the prefetch from the cache.
#   0x000  everything off at 0 wait states. Measured bit-exact, one
#          distinct value - the quiet cell, and the control that says the
#          instrument is capable of reading a null.
#
# All five are legal at the 16 MHz default, where the minimum LATENCY in
# voltage Range 1 is zero. PWR_CR1.VOS is NOT one of these words and
# regs.h says why at length: it is a measured null on this die, and
# putting it in the word would make trap 1's refusal rule
# two-dimensional.
BOARD_ACR_WORDS = 0x600 0x004 0x504 0x404 0x000
