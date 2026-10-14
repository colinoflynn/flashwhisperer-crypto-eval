# What is specific to MEASURING the STM32L152RE, as opposed to running it.
#
# The split from mcufw/stm32l152re/ is deliberate and thin, and it is worth
# saying which way it runs: anything that could CONTRADICT the board file
# belongs in the board file. A board file and a build file disagreeing
# about the clock produces numbers, not an error.
#
# So this holds only facts the measurement needs and the board does not:
# what the accelerator words mean, and how wide the flash reads are.
#
# IF THIS STAYS THIS SMALL, COLLAPSE IT into mcufw/<part>/eval.mk and drop
# the directory. It is kept separate for now because the second part ported
# will say whether there is really a per-part measurement surface or just a
# board file with two readers.

# The flash read width, as log2 of BYTES. NOT reported by the device and
# not discoverable from it - a per-part property that has to be measured
# once and then written down.
#
# It is what tools/attack.py needs for --line-shifts, and getting it wrong
# does not degrade gracefully: scoring an 8-byte row at the default of 4
# merges two real rows into one partition, tests a collision that does not
# exist, and returns a clean null that reads exactly like a part that does
# not leak.
#
# 3 = 8 bytes. RM0038: this part's flash is read 64 bits wide when ACC64 is
# set, which the only in-spec configuration at 32 MHz requires.
EVAL_LINE_SHIFT = 3

# The accelerator words worth sweeping, and what each one is. The host
# records whatever word the device reports; these are the ones that mean
# something on this part.
#
#   0x07  ACC64 | PRFTEN | LATENCY   64-bit reads, prefetch on, one wait
#                                    state. THE ONLY LEGAL CONFIGURATION AT
#                                    32 MHz, and what the board boots into.
#   0x05  ACC64 | LATENCY            prefetch OFF, one wait state.
#   0x06  ACC64 | PRFTEN             zero wait states - REFUSED by the
#                                    board at 32 MHz, and the refusal is
#                                    the point: SS_V_SET_ACR replies with
#                                    the readback, so a sweep records that
#                                    it did not take rather than crashing.
EVAL_ACR_WORDS = 0x07 0x05 0x06
