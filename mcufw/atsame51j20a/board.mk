# DESC: Cortex-M4F, 48 MHz, NVMCTRL line latch + CMCC, packed ACR word
# Microchip ATSAME51J20A on a SAM E51 Curiosity Nano.
#
# Two separately disableable structures on the flash read path - a CMCC on the
# core side and the NVMCTRL's own per-port line buffers - so the accelerator
# word is PACKED and the packing lives in mcufw/atsame51j20a/regs.h. The port has not
# been run on silicon; see the top of board.c.

# Soft float, matching the rig this port's registers come from. The cipher
# uses no floating point, and soft avoids having to enable CPACR before
# main() on a core that traps a disabled FPU rather than ignoring it.
BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the Curiosity Nano's nEDBG exposes one CDC port and it is
# wired to SERCOM5 on PB16/PB17. Nothing else is broken out as a console.
BOARD_LINKS    = vcp
# The packing is RWS | AUTOWS | CACHEDIS0/1 | CEN | ICDIS | DCDIS | CSIZESW;
# regs.h has the bit positions. These five are the short list, and
# eval/mcu/atsame51j20a/eval.mk says what each one is for.
#
# 0x2070 IS LAST ON PURPOSE. It clears the CMCC and raises CACHEDIS0, and
# raising that bit is one-way until a reset on this die, so a sweep that
# reaches it cannot come back. Put nothing after it.
BOARD_ACR_WORDS = 0x2010 0x2110 0x2510 0x2310 0x2070
