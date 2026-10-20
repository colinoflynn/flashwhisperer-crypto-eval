# What is specific to MEASURING the Renesas RA2A1, as opposed to running it.
#
# Same rule as the other parts: anything that could CONTRADICT the board file
# belongs in the board file, because a board file and a build file disagreeing
# about the clock produces numbers rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device and not
# discoverable from it.
#
# 3 = 8 bytes. THIS NUMBER IS NOT SETTLED ON THIS PART, which is why the flag
# below says no, and the two documents that could settle it disagree.
#
# research/reference/DATASHEETS.md, roster row, calls it "128-bit / 16 B
# (measured)" and the line structure "16 B = exactly 1 line (measured)", which
# would be a line shift of 4.
#
# research/targets/ra2a1.md, the dossier that took those measurements,
# contradicts its own headline in section 3b: the width is "disputed", the
# capacity probes that said 16 B step the working set by 16 bytes in firmware
# and so have 16-byte resolution by construction, and the alternating-pair
# probe, the only instrument with the resolution to discriminate, fits an
# 8-byte model exactly to three decimals where the 16-byte model is 0.400
# cycles out. One cycle elsewhere in the capacity probe is unexplained, so the
# dossier declines to replace the model outright.
#
# The attack agrees with 8. Re-scoring the archived 200 000-trace capture at
# both maps, 8 bytes roughly doubles every margin (1.897 against 0.936 on
# r1r2) and recovers two partial key sets at 12 500 traces where 16 bytes
# recovers nothing. Same bytes at 200 000 either way, so this buys margin
# rather than reach.
#
# So: 3 is the default here because it is what the better instrument and the
# better recovery both say, and PASS BOTH to tools/attack.py --line-shifts
# until the dossier's missing cycle is accounted for. Getting it wrong does
# not degrade gracefully; it returns a clean null that reads exactly like a
# part which does not leak.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = no

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN:
# FCACHEEN at bit 0, MEMWAIT at bit 4, FLWT at bits 8..10, because the three
# fields live in three registers across two peripherals and there is no
# FLASH_ACR here at all. Read mcufw/ra2a1/regs.h, not this file.
#
#   0x011  cache on, one memory wait, FLWT 0. What the part SHIPS doing, and
#          the cell that gave up the full AES-128 key at 200 000 traces.
#          Note that this already has the lowest FLWT: on this part zero wait
#          states in the flash field is the shipped state, not the fix.
#   0x010  the same with FCACHEEN cleared. This is the interesting one. On
#          this family the off switch removes the structure rather than
#          weakening it, and all three implementations go bit-exactly
#          constant, which is not true of any ST part on the bench.
#   0x711  cache on, FLWT 7: 16.160 cycles a read against 10.070 at FLWT 0,
#          measured at stride 64, so the gain factor is at its maximum with
#          the channel still open.
#   0x710  cache off at FLWT 7, where the field bites hardest: 43.203 cycles
#          a read against 16.090 at FLWT 0.
#   0x001  MEMWAIT 0. Prohibited above 32 MHz and REFUSED by the board, which
#          is the point: the readback shows it did not take instead of the
#          part stopping dead on its next instruction fetch.
EVAL_ACR_WORDS = 0x011 0x010 0x711 0x710 0x001
