# DESC: Cortex-M33, external QSPI flash via XIP, and the flash divider is a LIVE register
# Raspberry Pi RP2350 on a Pico 2.
#
# UNTESTED ON SILICON - it compiles and has never been flashed. See board.c.
#
# NOT AN RP2040 WITH A BIGGER NUMBER. Five differences in the peripheral and
# flash maps produce code that assembles, runs and measures something else;
# they are listed at the top of regs.h, and 0x18000000 - the SSI register
# block on the RP2040 - being the cache maintenance window here is the worst
# of them.
#
# What makes it worth having is the opposite of what makes the RP2040 worth
# having. The research tree's standard build on this part performs NO FLASH
# ACCESS AT ALL during the S-box encryption, so it is recorded as not broken
# rather than as safe - the question was never put to it. A build that places
# the table in the XIP window and proves the address is in that window is
# what puts it. And QMI_M0_TIMING.CLKDIV, the flash SCLK divider, is WRITABLE
# AT RUNTIME, so the gain knob the RP2040 can only vary by relinking a boot2
# is one word here. See research/targets/rp2350.md in the research tree.
#
# pyocd.target holds `rp2350`, which needs a recent pyocd: `python -m pyocd
# list --targets` may mark it `pack`, in which case
# `python -m pyocd pack install rp2350` first, the same as the H723 and the
# F746.
#
# THERE IS NO BOOT2 IN THIS IMAGE, unlike the RP2040 port beside it. This
# part's bootrom sets up XIP itself and boots the IMAGE_DEF block in the
# first 4 KB; startup.c carries that block and memory.ld pins it.

# SOFT FLOAT ON A PART THAT HAS AN FPU, deliberately. The cipher uses no
# floating point, and a hard-float ABI would need SCB->CPACR enabled before
# the first FP instruction - which in this project has previously meant -O3
# vectorising a startup copy loop into VFP and the board booting into a NOCP
# fault that says nothing at all. atsame51j20a and atsamg55j19 are soft for the
# same reason.
BOARD_CPU      = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# Only `vcp`: UART0 on GP0/GP1. A Pico 2 has no on-board bridge to the host,
# so this is the probe's CDC or an adapter on the header - which is all
# LINK= describes.
BOARD_LINKS    = vcp
# The accelerator words worth sweeping, reading the packing off regs.h:
# CLKDIV in bits 0..7, and XIP_CTRL shifted left by 8 above it, so
# EN_SECURE is bit 8, EN_NONSECURE bit 9 and SPLIT_WAYS BIT 17 (not bit 9 -
# that is its position in XIP_CTRL, not in the word).
#
#   0x00304  cache on, ways unsplit, CLKDIV 4. The reset value of CLKDIV and
#            what the board boots into.
#   0x00004  cache off, CLKDIV 4. With both enables clear the accesses go
#            straight to the serial link.
#   0x00308  cache on, CLKDIV 8 - half the flash clock, a dearer miss.
#   0x00310  cache on, CLKDIV 16 - dearer again. THIS IS THE AXIS THE RP2040
#            CANNOT SWEEP without relinking, and it is the closest thing on
#            either part to an ST wait state.
#   0x20304  cache on with SPLIT_WAYS - the 2-way cache becomes two
#            direct-mapped half-caches, one per security state. A
#            direct-mapped cache makes collisions deterministic, so this is
#            a configuration that could CREATE a channel rather than
#            mitigate one, which is the interesting direction.
#   0x00301  CLKDIV 1 - REFUSED by the board, and the refusal is the point:
#            SS_V_SET_ACR replies with the readback, so a sweep records that
#            it did not take rather than hanging the part on a flash that
#            has stopped answering.
BOARD_ACR_WORDS = 0x00304 0x00004 0x00308 0x00310 0x20304 0x00301
