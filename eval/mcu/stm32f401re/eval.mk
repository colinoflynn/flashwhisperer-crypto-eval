# What is specific to MEASURING the STM32F401RE.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes, and this is now MEASURED on this part rather than
# inherited from the F429. The evidence is a verified key recovery: the
# compact-T-table build at acr 0x700 gives up the exact AES-128 master
# key at 100 000 traces scored at line_shift 4, checked against the
# victim's compiled-in key and against a known plaintext/ciphertext
# pair. The losing widths on the same capture are the other half of it -
# ls=2 reached 7/16 and ls=1 only 3/16.
#
# Note what the 4 means here. The compact layout runs T-tables for
# rounds 1..9 and a BYTE S-BOX final round, and the attack reads the
# last round - so 16 entries per row is 16 one-byte entries, i.e. a
# 16-byte read. It is not 16 four-byte entries.
#
# The byte-S-box build is a weaker target on this part and did not
# recover: 2/16 at 25 000 traces, best at the same ls=4.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = yes

# LATENCY | PRFTEN | ICEN | DCEN, as on the F429.
#
#   0x700  prefetch + both ART caches, zero wait states
#   0x300  prefetch + I-cache
#   0x500  prefetch + D-cache
#   0x100  prefetch only
#   0x000  everything off
#
# On the F429 the DCEN bit is the one that gives data reuse and ICEN and
# PRFTEN give none, so 0x500 against 0x100 is the pair to watch. Whether
# that holds here is the question, not the assumption.
EVAL_ACR_WORDS = 0x700 0x300 0x500 0x100 0x000
