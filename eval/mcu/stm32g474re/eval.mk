# What is specific to MEASURING the STM32G474RE.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 3 = 8 bytes, measured on silicon: this part reads 64 bits at a time,
# the narrowest of the parts here (the F429 is 16 bytes and the two M7s
# are 32). So a byte S-box wants --line-shifts 3.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = yes

# LATENCY | PRFTEN | ICEN | DCEN.
#
#   0x500  prefetch + D-cache, 0 wait states. DCEN IS THE CHIP DEFAULT
#          and a compact T-table leaks here.
#   0x100  prefetch only - the same word with DCEN cleared. A byte S-box
#          still gives 12-15/16 there, so clearing it is not a fix.
#   0x700  everything on.
#   0x300  prefetch + I-cache: the I-cache gives no data reuse, so this
#          should match 0x100. A difference would be the finding.
#   0x000  everything off. Measured BIT-EXACT by the research tree, and
#          it is also the correct setting at 16 MHz - the quiet cell is
#          not the slow one on this part, and tables in SRAM are the
#          fastest placement as well as the safe one.
EVAL_ACR_WORDS = 0x500 0x100 0x700 0x300 0x000
