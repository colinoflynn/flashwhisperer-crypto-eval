# DESC: Cortex-M4, 8 MHz, the smallest FLASH_ACR here (LATENCY/HLFCYA/PRFTBE)
# STM32F303RE on a NUCLEO-64.
#
# UNTESTED ON SILICON: written from rigs/fw_f303_aes/ in the research
# tree, built, never run. See board.c for what that leaves open.
#
# The part with no cache on either side and no ART, so its accelerator
# word carries a knob no other part here has (HLFCYA, half-cycle flash
# access) and lacks the four every F4/G4 port sweeps.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the Nucleo-64 wires its VCP to USART2 and breaks out no
# second UART by default.
BOARD_LINKS    = vcp
# LATENCY | HLFCYA | PRFTBE. PRFTBS is read-only and never appears in a
# requested word; it comes back in the readback when the prefetch is on.
#
#   0x10  prefetch on, 0 wait states - the reset state and the default
#   0x00  prefetch off, 0 wait states
#   0x12  prefetch on, 2 wait states over-programmed, as the gain knob
#   0x18  prefetch on plus half-cycle access
#   0x08  half-cycle access with the prefetch off
#
# All five are legal at the 8 MHz default: HLFCYA needs 8 MHz or below
# with the PLL off, and toggling PRFTBE needs under 24 MHz. At a higher
# SS_CLK_MHZ ssb_acr_set refuses the HLFCYA words and the prefetch
# toggle, and the host sees the refusal in the readback.
BOARD_ACR_WORDS = 0x10 0x00 0x12 0x18 0x08
