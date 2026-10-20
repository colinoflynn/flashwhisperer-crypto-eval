# DESC: Cortex-M0+, 48 MHz, 64 B unified NVM cache, and a constant-time read mode
# Microchip ATSAMD21J18A on a SAM D21 Xplained Pro.
#
# UNTESTED ON SILICON - it compiles and has never been flashed. See board.c.
#
# A Cortex-M0+, so there is NO DWT cycle counter: SysTick is the only time
# base, and TIMING=dwt reports no counter rather than inventing one.
# It is also the only part here whose read path has a READMODE field that
# prices a miss separately from the wait state, which is why it leaks at zero
# wait states under LOW_POWER.
#
# It is also the only part in the tree whose vendor ships a documented
# constant-time read mode (NVMCTRL.CTRLB.READMODE = DETERMINISTIC), which is
# why its accelerator word has a field the ST parts do not. The packing is in
# regs.h.
#
# pyocd.target holds `atsamd21j18a`, which `python -m pyocd list --targets`
# resolves but marks `pack`, so flashing needs
# `python -m pyocd pack install atsamd21j18a` first, the same as the H723 and
# the F746.

BOARD_CPU      = -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: the EDBG's virtual COM port is wired to PA22/PA23, which is
# SERCOM3 on mux C. The EXT headers do carry other SERCOM pads, but no rig
# in the research tree has brought one up, so no second link is declared.
BOARD_LINKS    = vcp
# Accelerator words worth sweeping, reading the packing off regs.h: RWS in
# bits 0..3, READMODE in bits 8..9, CACHEDIS at bit 16.
#
#   0x00001  RWS 1, READMODE = NO_MISS_PENALTY, cache on. WHAT THE PART
#            SHIPS AS and what the board boots into at 48 MHz: the NVMCTRL
#            reset values apart from the wait state, which 48 MHz requires.
#   0x00101  the same with READMODE = LOW_POWER, which is the vendor
#            DOCUMENTING a miss-dependent wait state.
#   0x00201  the same with READMODE = DETERMINISTIC, the vendor's own
#            mitigation for this channel.
#   0x10001  the same with the cache disabled.
#   0x00000  RWS 0 - REFUSED by the board at 48 MHz, and the refusal is the
#            point: SS_V_SET_ACR replies with the readback, so a sweep
#            records that it did not take rather than crashing.
#
# REACHING RWS 0 LEGALLY MEANS THE 8 MHz BUILD POINT, which is `CLK=8` on
# the make line:
#
#     ./tools/build.sh MCU=atsamd21j18a LIB=refsbox CLK=8
#
# That is a real axis in build.mk and not a define this file invents: it
# puts -DSS_CLK_MHZ=8 on the compile line and `-clk8` in the variant
# string, so the 8 MHz image gets its own build directory and its own
# capture name. It used to be an edit to the `#define SS_CLK_MHZ 48` in
# board.c, because build.mk composed COMMON_DEFS itself and had no hook for
# an extra -D - `SS_CLK_MHZ=8` on the make line was accepted and silently
# ignored, and the variant string carried no clock either, so the 48 MHz
# and 8 MHz images landed in one directory under one name.
#
# The 48 MHz default stays in board.c and `CLK=48` reproduces it byte for
# byte; CLK=8 does not, which is how the hook was checked. Any other value
# is an #error in board.c rather than a quiet fall back to 48.
BOARD_ACR_WORDS = 0x00001 0x00101 0x00201 0x10001 0x00000
