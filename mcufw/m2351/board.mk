# DESC: Cortex-M23, 12 MHz, FMC_CYCCTL - one register is the whole read path - UNTESTED port
# Nuvoton M2351 (M2351KIAAE on a NuMaker-PFM-M2351).
#
# The first Nuvoton part here, and not the first Cortex-M23: the RA2A1 is
# ARMv8-M Baseline too. Those two agree about the core and about nothing on
# the flash side - 512 lines of 8 B against 1 line of 16 B, no off switch
# against a working one - which is this project's standing negative result
# with the core held fixed.
#
# COMPILED ONLY. No board was attached and nothing has been flashed. board.c
# says what that means and names what could not be established.
#
# ARMv8-M BASELINE, NOT MAINLINE. Do not "fix" this to cortex-m33 by analogy
# with the L552: M33 is Mainline, has IT blocks, and would compile the cipher
# into different code on a core that cannot run it. Baseline having no IT
# blocks is itself load-bearing on this part - a branchy xtime becomes a real
# conditional branch and makes the cipher data-dependent before the flash is
# involved at all.

BOARD_CPU      = -mcpu=cortex-m23 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# `vcp` is the only value build.mk accepts that fits, and on this part it
# does NOT mean a virtual COM port. There is no UART to the host at all: the
# Nu-Link ICE's second USB interface is a bare CDC-DATA descriptor with no
# control interface in front of it, so no host driver can ever bind it. The
# host link is the debug probe. board.c implements it as a RAM mailbox and
# says what is missing on the host side.
BOARD_LINKS    = vcp
# FMC_CYCCTL.CYCLE (bits 0..3) | FADIS (bit 8). Not a packing: that register
# is the entire flash read path on this part, so the word is the register.
#
#   0x101  software-owned CYCLE 1. The lowest reachable wait state and the
#          nearest thing to an accelerator-off arm this part has, which is
#          not very near: a miss still costs about 2 cycles.
#   0x102  CYCLE 2, and
#   0x103  CYCLE 3. A GAIN FACTOR, not more information: the measured spread
#          goes 22.87 / 32.93 / 43.03 across the three while the channel
#          stays the same channel.
#   0x001  FADIS clear: the hardware auto-tunes CYCLE and settles on 1 at
#          12 MHz, which is what the part ships doing and what ssb_init
#          leaves it in. The word is 0x001 rather than 0x000 because the
#          readback carries the auto-tuned CYCLE, and tools/capture.py
#          captures nothing when the readback and the request differ.
#
# THERE IS NO CACHE-OFF WORD IN THIS LIST AND THAT IS THE FINDING. CYCLE = 0
# is documented as "cache is disabled by hardware" and cannot be written on
# this die - the field floors at 1 - so 0x100 is refused by the board rather
# than recorded as a configuration that silently became CYCLE 1. Reserved
# values 4..15 do stick, and are refused for a different reason; board.c
# separates the two.
BOARD_ACR_WORDS = 0x101 0x102 0x103 0x001

# A NOTE ON pyocd.target, WHICH HAS TO LIVE HERE because tools/capture.py
# reads that file whole and strips it, so a comment in it would be passed
# through as the target name.
#
# `python -m pyocd list --targets` has no m2351 row: the builtin Nuvoton
# targets stop at the M252, M263, M2354, M467 and M487. `python -m pyocd
# pack find m2351` does list M2351KIAAE, from Nuvoton.NuMicro_DFP 1.3.26,
# not installed on this machine - so the name in pyocd.target needs
# `python -m pyocd pack install m2351kiaae` before it resolves.
#
# AND THAT STILL WILL NOT FLASH THIS BOARD. The on-board Nu-Link ICE speaks
# Nuvoton's own HID protocol and pyocd does not list it as a probe at all,
# not even as an unsupported one, so capture.py's pyocd path cannot reach
# it whatever target is named. The rig programs this part by driving the FMC
# ISP registers over SWD through results/m2351/nulink.py in the
# flash-accelerator-tests tree. Wiring that up is a change to
# tools/capture.py, which is outside this port.
