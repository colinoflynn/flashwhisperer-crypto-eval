# DESC: Cortex-M4F, 64 MHz, NO wait-state register and NO data cache
# Nordic nRF52840 on an nRF52840-DK.
#
# This port exists to BOUND the others:
# no FLASH_ACR, no latency field anywhere on the die, and the only cache is
# a 2 KB instruction cache on the ICODE bus. The accelerator word is
# NVMC.ICACHECNF and nothing else; see mcufw/nrf52840/regs.h for why that
# word cannot reach the data path, and board.c for the fact that this port
# has never been run on silicon.

BOARD_CPU      = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the DK's J-Link OB virtual COM port is UART0 on P0.06/P0.08,
# which is the only console any nRF rig in the research tree has used.
BOARD_LINKS    = vcp
# CACHEEN | CACHEPROFEN, and that is the entire axis this part has.
#
#   0x000  cache off. The reset state.
#   0x001  cache on.
#   0x101  cache on with IHIT/IMISS counting, which is the only setting in
#          which ssb_has_monitors reports a counter.
#
# Three words rather than the five an ST part sweeps, because there is no
# latency to cross them with.
BOARD_ACR_WORDS = 0x000 0x001 0x101

# pyocd.target holds `nrf52840`, which `python -m pyocd list --targets`
# resolves built in - no pack install, unlike the H723 and the F746.
#
# ONE WARNING ABOUT FLASHING THIS PART, and it is the one that cannot be
# undone. If the board arrives with APPROTECT set, CONNECTING TO IT AT ALL
# MASS-ERASES IT. So the habit of dumping a board's existing image before
# reflashing, so that another campaign's firmware can be put back afterwards,
# IS NOT AVAILABLE HERE: by the time a debugger has attached there is nothing
# left to read. No step in this port assumes otherwise and none added to it
# should.
