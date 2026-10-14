# What is specific to MEASURING the STM32F746ZG.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 5 = 32 bytes, measured on silicon, the same as the H723 and twice the
# F429's. So a byte S-box wants --line-shifts 5 and a 4-byte word table
# wants 3; pass both, because the wrong one returns a clean null rather
# than a degraded answer.
EVAL_LINE_SHIFT = 5

# The accelerator words worth sweeping. The packing is this part's own -
# FLASH_ACR's bits verbatim in the low half, the two L1 cache enables at
# bits 16 and 17. See mcufw/stm32f746zg/regs.h.
#
#   0x30307  LATENCY 7 | PRFTEN | ARTEN, both L1 caches on. THE VENDOR'S
#            OWN CONFIGURATION for this family.
#   0x00307  the same accelerator word with both caches off.
#   0x10307  I-cache only.
#   0x20307  D-cache only. The D-cache is the knob that reaches the read
#            latch on this part, and what it does is replace it: 32 B x 1
#            becomes 128 B x 8.
#   0x00007  LATENCY only - no prefetch, no ART, no caches.
#
# Expect 0x00307 and 0x00007 to be indistinguishable. The ART is on the
# ITCM alias and the victim runs from the AXI alias, so it cannot reach
# the data path; the research tree measured those two byte-identical in a
# cipher scan and found no effect in 8 matched pairs of a knob map. A
# null there is the expected result, not a broken axis.
EVAL_ACR_WORDS = 0x30307 0x00307 0x10307 0x20307 0x00007
