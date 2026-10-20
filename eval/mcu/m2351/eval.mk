# What is specific to MEASURING the Nuvoton M2351, as opposed to running it.
#
# Same rule as the other parts: anything that could CONTRADICT the board file
# belongs in the board file, because a board file and a build file
# disagreeing about the clock produces numbers rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device and not
# discoverable from it.
#
# 3 = 8 bytes, and on this part it is settled. research/reference/DATASHEETS.md
# section A, the roster row for the M2351, gives the flash read width as an
# "8-byte fill granule" and the structure that holds it as "512 lines x 8 B,
# 1 way, index A[11:3] - gaps of 4096/8192/12288 thrash completely, 4088 and
# 4104 cost nothing". Both are measured on the die rather than read off a
# datasheet: the granule is the knee of a fixed-span stride walk and the
# index function is where the conflicts fall.
#
# The recovery agrees with the probe, which is the check that matters. The
# same roster's section B row records the only full key off this part as
# "200 000 (sbox_alt, -O3, S-box at 0x8800, 8-byte row map, state)" -
# verified cryptographically, and scored with exactly this row map.
#
# Getting it wrong does not degrade gracefully: scoring an 8-byte row at the
# default of 4 merges two real rows into one partition, tests a collision
# that does not exist, and returns a clean null that reads like a part which
# does not leak.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = yes

# AND THE ROW MAP IS NOT ENOUGH ON ITS OWN. A trace count from this part is
# under-specified without the S-box's runtime address - the 200 000 above is
# at 0x8800, offset 0 in its 8-byte row - and without the build: at -O2 this
# part's byte S-box is bit-exactly constant, sd 0.0000 over 4 096 traces, and
# at -O3 the same source is sd 32.93 and a complete break. Screening only the
# standard build and stopping would write this part up as constant time.
# `make addr` prints the address; record it with every capture.

# The accelerator words worth sweeping. THE WORD IS FMC_CYCCTL ITSELF, CYCLE
# in bits 0..3 and FADIS in bit 8, because that one register is the entire
# flash read path on this part. Nothing is packed in from elsewhere; read
# mcufw/m2351/regs.h, not this file.
#
#   0x101  software-owned CYCLE 1, the lowest value the field will take.
#   0x102  CYCLE 2, and
#   0x103  CYCLE 3. These are the gain ladder: sd 22.87 / 32.93 / 43.03 at
#          CYCLE 1 / 2 / 3. More wait states is a bigger signal and not more
#          information, which is the same conclusion the F429's 0x705 row
#          records, reached here on a part whose cache cannot be turned off.
#   0x001  FADIS clear, the shipped configuration: the hardware auto-tunes
#          CYCLE and settles on 1 at 12 MHz. The word is 0x001 and not
#          0x000 because the readback carries the auto-tuned CYCLE.
#
# THERE IS NO CACHE-OFF ROW AND THAT ABSENCE IS THE RESULT. CYCLE = 0 is the
# vendor's documented "cache is disabled by hardware" setting; it cannot be
# written on this die, the field floors at 1, and the board refuses it so the
# sweep records a refusal rather than a cell that silently ran at CYCLE 1
# under the name 0x100. The mitigation that works here is the residence axis
# this repository already builds: tables in SRAM are bit-exact AND 1.07 to
# 1.13 times faster than every flash configuration, so RAMTAB=1 is not only
# the control on this part, it is the fix.
EVAL_ACR_WORDS = 0x101 0x102 0x103 0x001
