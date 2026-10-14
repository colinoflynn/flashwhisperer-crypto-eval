# What is specific to MEASURING the STM32F429ZI, as opposed to running it.
#
# This file was missing while the part was already being measured, so the
# one number the attack cannot work without lived in prose in
# results/stm32f429zi/README.md and nowhere a script could reach.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes. Established by the sweep rather than asserted: on the
# 250 000-trace capture of this part, line_shift 4 reaches the exact
# master key at 100 000 while line_shift 2 gives 1/16 at every count on
# the same file. That is the attack discovering that this last round
# reads the byte S-box in a 16-byte row.
EVAL_LINE_SHIFT = 4

# The accelerator words worth sweeping: LATENCY | PRFTEN | ICEN | DCEN,
# the plain FLASH_ACR layout, which is this part and not the H723.
#
#   0x700  prefetch + both ART caches, zero wait states. The arm the
#          recovery above came from.
#   0x705  the same at five wait states: eight times the spread, and NOT
#          the easier target - wait states are a gain factor on the
#          measurement rather than more information.
#   0x300  prefetch + I-cache, D-cache off.
#   0x500  prefetch + D-cache, I-cache off.
#   0x100  prefetch only.
#   0x000  everything off.
EVAL_ACR_WORDS = 0x700 0x705 0x300 0x500 0x100 0x000
