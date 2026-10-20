# DESC: Cortex-M0, 32 MHz, no accelerator, no read latch (UNTESTED, no console)
# Infineon XMC1100-Q024F0064, as on an XMC2Go (KIT_XMC_2GO_XMC1100_V1).
#
# The first part here with nothing on the data side at all: no cache, no
# prefetch buffer, and no read latch either, measured absent rather than
# merely undocumented. The word SS_V_SET_ACR carries is the flash read
# path - the wait-state scheme, the wait state and the read margin - and
# regs.h says why that is not an accelerator.
#
# UNTESTED ON SILICON, and two hooks are unimplemented: the console (a USIC
# channel in ASC mode, whose registers are not established anywhere this
# port could read) and the trigger pin. Read the banner in board.c before
# planning anything with it.

BOARD_CPU      = -mcpu=cortex-m0 -mthumb -mfloat-abi=soft
BOARD_SRC      = $(MCUDIR)/board.c $(MCUDIR)/startup.c
BOARD_INC      = -I$(MCUDIR)
# `vcp` names the J-Link OB's virtual COM port on the XMC2Go, which does
# exist on the board. It is declared because the build requires a link to
# be named and there is no spelling for "none", NOT because this port can
# speak on it: see board.c. Nothing is wired to the headers by this port
# either, so `header` is deliberately absent rather than untested.
BOARD_LINKS    = vcp
# The read-path words worth sweeping. Reading the packing off regs.h, bit 0
# is NVMCONF.WS, bit 1 is CONFIG1.FIXWS and bits 4..5 are NVMCONF.HRLEV.
#
#   0x01  adaptive scheme, WS = 1. WHAT THE PART SHIPS IN, and on this
#         family it is the NOISY cell: above roughly 21 MHz the adaptive
#         circuit does not resolve the same way twice, so one encryption
#         varies by about +/-100 cycles. That jitter was measured not to be
#         information - a fixed plaintext has the same spread as random
#         ones - and quoting it as a channel would have been the cleanest
#         false positive the research tree has produced.
#   0x03  fixed scheme, WS = 1. The deterministic cell, and the one every
#         bit-exact result on this part was taken in.
#   0x02  fixed scheme, WS = 0. REFUSED by the board above 16 MHz and the
#         refusal is the point: it was tried on silicon and stopped the
#         part answering, so SS_V_SET_ACR replies with the readback and a
#         sweep records that it did not take rather than hanging.
#   0x13  fixed WS = 1 with HRLEV = 1, the read margin. Costs 64 % on a
#         whole encryption, which is a bigger lever than the wait state,
#         and is still one distinct cycle count: a gain factor, not
#         information.
BOARD_ACR_WORDS = 0x01 0x03 0x02 0x13
