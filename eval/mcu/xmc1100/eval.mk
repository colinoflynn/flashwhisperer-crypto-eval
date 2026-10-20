# What is specific to MEASURING the Infineon XMC1100, as opposed to
# running it. Anything that could CONTRADICT the board file belongs in the
# board file, because a board file and a build file disagreeing about the
# clock or the wait state produces numbers rather than an error.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# NOT MEASURED ON THIS PART, and the reason is a result rather than an
# omission. research/reference/DATASHEETS.md section A records this part's
# flash read width as "not observable - nothing holds a read", and its
# data-side structure as "none": every stride from 1 to 256 bytes costs the
# same per read, so there is no step from which a width could be named.
# Reading one byte twice costs exactly what reading two rows 4 KB apart
# costs, to the cycle.
#
# 4 is therefore what the research tree's collision model ASSUMED for this
# part (results/xmc1100/ttable_collisions.py calls line_sequence with
# line_bytes=16), not what any probe returned. It is here so a scoring run
# has a defined starting point; treat it as a guess and pass several
# widths, which is what the attack wants anyway. On a part with no row
# structure the row partition is not expected to carry anything at any
# width - 0/16 key bytes on 400 000 traces, with the margin falling as
# 1/sqrt(n), which is the signature of no signal rather than of a wrong
# width.
#
# The instruction-side fetch width is a separate question and is also
# unnamed: the data-side probe cannot see it.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = no

# The read-path words worth sweeping. They are listed with what each one
# means in mcufw/xmc1100/board.mk, and the packing is in
# mcufw/xmc1100/regs.h - bit 0 WS, bit 1 FIXWS, bits 4..5 HRLEV. This is
# not an accelerator word: the part has no accelerator.
#
#   0x01  adaptive, WS 1. As shipped, and the noisy cell.
#   0x03  fixed, WS 1. The deterministic cell.
#   0x02  fixed, WS 0. Refused above 16 MHz, and the refusal is the point.
#   0x13  fixed WS 1 with the read margin raised. A 64 % gain factor.
EVAL_ACR_WORDS = 0x01 0x03 0x02 0x13
