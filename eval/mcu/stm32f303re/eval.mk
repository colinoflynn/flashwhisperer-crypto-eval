# What is specific to MEASURING the STM32F303RE.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 3 = 8 bytes, and it is MEASURED on this die rather than inferred from
# the family. Two places in the research tree carry it.
#
# research/reference/DATASHEETS.md, section A "What the flash path is",
# gives the part one row and the row reads, in its first four cells:
#
#   "STM32F303 (x422 CW303, x446 NUCLEO) | ST, Cortex-M4 | 64-bit |
#    none"
#
# and its instruction-side cell reads "prefetch buffer, 8 B, PRFTBE".
# 64-bit is 8 bytes, and the "none" is the data-side structure: this
# part holds no row on the data side at all.
#
# research/targets/stm32f303.md section 3, "Geometry, measured", gives
# the evidence rather than the number:
#
#   "flash read width | 8 bytes, measured | S stub-spacing sweep: the
#    split is present at spacing 2 and 4 and vanishes at 8, 16 and 32.
#    The spacing at which the channel goes silent is the word width."
#
# That sweep was run on the NUCLEO-F303RE die (DEV_ID 0x446, F303xD/E,
# read from DBGMCU_IDCODE), which is this part and not the CW303's
# F303xB/C.
#
# ONE DISAGREEMENT, RECORDED RATHER THAN AVERAGED. Section 8 of that same
# dossier, "What this does not establish", still says the geometry was
# not re-measured on this die and that the read width is carried from the
# CW303. research/FINDINGS_LEDGER.md is the normative register and it
# sides with section 3: its F303 row says the stub-spacing sweep "goes
# silent at 8 bytes" on the second die, and a later line says in terms
# that "the flash read width and the instruction-side period are now
# measured on this die". So section 8 is the stale half. The one geometry
# claim still carried from the CW303 is the ABSENCE of a data-side read
# latch, which is not this number.
EVAL_LINE_SHIFT = 3
EVAL_LINE_SHIFT_MEASURED = yes

# LATENCY | HLFCYA | PRFTBE, which is the whole writable accelerator on
# this part. There is no ICEN, no DCEN and no ICRST/DCRST to sweep.
#
#   0x10  prefetch on, 0 wait states. The reset state, and the
#         configuration the research tree's 68 constant-time cells were
#         mostly taken at.
#   0x00  prefetch off, 0 wait states. Not a cheap control: the dossier
#         measures the prefetch off as +64 % on a whole encryption, so
#         this cell is slow rather than quiet.
#   0x12  prefetch on, 2 wait states. Over-programmed at 8 MHz and
#         deliberately so - a wait state is a gain factor on the
#         measurement rather than more information, which is the F429
#         row's lesson and is why this is in the list at all.
#   0x18  prefetch on plus half-cycle flash access. HLFCYA has no
#         counterpart on any other part in this tree: it changes flash
#         access timing directly instead of through a wait state. Legal
#         only at 8 MHz or below with the PLL off, which is where this
#         port runs.
#   0x08  half-cycle access with the prefetch off.
#
# WHAT TO EXPECT, so that a null here is read correctly. The research
# tree reports this part's DATA path as bit-exactly constant - 68 of 68
# cells at one distinct cycle count, across the whole FLASH_ACR axis,
# both optimisation levels and the whole 8-byte row offset space - and a
# 25 000-trace capture recovering 0/16 with margin exactly 0.000. The
# live instruction-path channel on the same die is not what this victim
# measures. So a flat result here is the expected one and is not
# evidence the rig is broken; what makes it readable is still the RAM
# twin and a positive control, not the shape of the number.
EVAL_ACR_WORDS = 0x10 0x00 0x12 0x18 0x08
