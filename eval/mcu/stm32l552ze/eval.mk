# What is specific to MEASURING the STM32L552ZE.
#
# Same rule as the other parts: anything that could CONTRADICT the board
# file belongs in the board file, because a board file and a build file
# disagreeing about the clock produces numbers rather than an error.
#
# The port itself is UNTESTED on silicon. What is below is not: it is read
# off the research tree's measurements of this die, with the source named
# for each number so the next person can check it rather than inherit it.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 3 = 8 bytes. The research tree's
# research/reference/DATASHEETS.md gives this part's flash read width as
# "64-bit (measured)" in its family table, and its per-part table records
# the data-side structure as "read latch, two rows", "2 x 8 B, indexed on
# A[3]". Both say the same thing: a flash read here brings back 64 bits.
#
# DO NOT USE THE CACHE LINE HERE. The same tables give the ICACHE line as
# "16 B (measured)", and 16 is the wrong number for this knob: the
# dossier (research/targets/stm32l552.md section 0 and 5e) records that
# the published attack modelled the collision at 16-byte granularity and
# that telling it the 8-byte latch entry instead recovers the exact master
# key from 50 000 traces per read order, where the 16-byte grouping had
# not produced a key at 100 000. So 4 is not a coarser version of 3 here,
# it is the setting that returns a clean null.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN -
# FLASH_ACR here holds LATENCY and LVEN and no accelerator bits at all, so
# mcufw/stm32l552ze/regs.h packs the ICACHE enable and WAYSEL alongside
# the latency. Read it there, not here.
#
#   0x304  LATENCY 4, cache on, 2-way. The state the board boots into and
#          the one the research tree's L552 numbers were taken in.
#   0x104  LATENCY 4, cache on, 1-way. WAYSEL does not halve the cache,
#          it re-indexes it - all 8 KB in both settings, 512 sets at
#          1-way against 256 at 2-way, counted with the hardware miss
#          counter (dossier section 3b). So this is a different set of
#          collisions at the same capacity, and the dossier's one
#          cache-on key recovery is a 1-way cell.
#   0x004  LATENCY 4, cache off. The cell the full-key result is usually
#          quoted from. "Off" does not mean no reuse: the two-entry
#          8-byte read latch underneath has no disable bit anywhere on
#          this part.
#   0x00F  LATENCY 15, cache off. The dossier's section 5h attack cell,
#          and the one with the largest per-collision differential.
#   0x000  LATENCY 0, cache off. EXPECT THIS CELL TO BE SILENT, and that
#          is the finding rather than a broken axis: 150 000 plaintexts
#          gave one distinct timing value and 0 of 8 key bytes (dossier
#          section 7). The latch is still there; it is worth nothing when
#          a miss costs no wait states. It is only reachable at low
#          clock - this board runs HSI16 at 16 MHz, and an L552 at its
#          110 MHz maximum needs five wait states.
#
# One prediction worth writing down before the capture, because it was
# measured on this die and is the opposite of the obvious: the four
# cache-off cells above zero are expected to need the SAME number of
# traces, not fewer at higher latency. The dossier's section 7 found
# LATENCY 2, 4, 8 and 15 gave byte-identical attack ladders, because an
# exact cycle counter scales signal and noise together.
EVAL_ACR_WORDS = 0x304 0x104 0x004 0x00F 0x000
