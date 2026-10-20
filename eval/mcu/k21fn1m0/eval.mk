# What is specific to MEASURING the NXP Kinetis MK21FN1M0VMC12.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes, and it is MEASURED on this part rather than inherited from
# anything. research/reference/DATASHEETS.md, in the table whose "flash read
# width" column is the roster entry for each part, gives the MK21FN1M0 row as
#
#   "128-bit (16 B) | single-entry buffer (B0SEBE) + speculation buffer +
#    cache, all three separable | 1 x 16 B"
#
# and research/targets/k21fn1m0.md records how that was established rather
# than read off a datasheet: "The latch is real, depth 1, granularity 16 B
# [...] That is the flash read width, independently confirmed by a stride
# walk saturating at 16 and by PFB0CR.B0MW = 0b10." Its summary table
# repeats the two: "stride walk saturates at stride 16; and PFB0CR.B0MW =
# 0b10 agrees", with the latch step "lands exactly at gap 16".
#
# So there are three independent lines here, which is one more than most
# parts have: a timing probe, a pair probe, and a read-only hardware field
# that says what the array returns. They agree.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = yes

# The packed accelerator word - see mcufw/k21fn1m0/regs.h for the layout.
# enables | CRC<<5 | CLCK_WAY<<8 | (OUTDIV4-1)<<12, with bits 21..16 the
# B0RWSC and B0MW readbacks, which a set ignores.
#
#   0x101F  all five FMC enables on, LRU, no way locked - the reset state
#   0x1007  single entry buffer + both prefetches, both caches off
#   0x1006  both caches off AND the single entry buffer off
#   0x1001  single entry buffer only
#   0x1000  everything off
#
# WHAT THESE FIVE CELLS ARE FOR. This part is the one that separates a read
# latch from a cache with register bits instead of with a probe: B0SEBE
# enables a 128-bit single entry buffer "in response to flash read
# accesses", and B0DCE enables a separate 256 B data cache, and the two are
# independently switchable. 0x1001 against 0x1006 is therefore the pair that
# isolates the latch, and 0x1006 is the configuration the dossier records as
# closing the channel bit-exactly at a 43.8 % runtime cost.
#
# EVERY CELL HERE KEEPS OUTDIV4 AT 2, the measured reset divider, so the
# derived wait state stays put and these five differ only in the
# accelerator. The wait-state axis on this part is that divider rather than
# a latency field, because B0RWSC is read-only; sweeping it means sweeping
# bits 15..12 of this same word and reading B0RWSC back out of bits 19..16
# to see what the FMC decided. That readback is a confirmation the ST parts
# cannot give, and it is the reason the divider is in the word at all.
EVAL_ACR_WORDS = 0x101F 0x1007 0x1006 0x1001 0x1000
