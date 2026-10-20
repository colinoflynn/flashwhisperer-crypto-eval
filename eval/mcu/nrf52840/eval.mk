# What is specific to MEASURING the nRF52840, as opposed to running it.

# The flash read width, as log2 of BYTES. NOT reported by the device.
#
# 4 = 16 bytes, and it is measured on silicon rather than inferred. The
# research tree's roster, research/reference/DATASHEETS.md, carries this
# part twice and both rows say the same thing: the flash-path table gives
# its read width as "128-bit (16 B)" with the data-side structure a read
# latch of "1 x 16 B", and the fuller row records "128-bit / 16 B
# (measured)" together with "a 1-entry 16-byte read latch that no register
# reaches (measured; documented nowhere)".
#
# That latch is the whole data-side mechanism on this part. There is no
# data cache and no wait-state register, so there is nothing else for a
# 16-byte row to be an artefact of.
#
# The width also shows up in what the attack costs: the same firmware
# recovers the key in 6 250 traces when the table lands 16-byte aligned,
# and in 12 500 and 25 000 at two link-time accidents of that same
# firmware. A trace count here is under-specified without the table's
# address, which is why build.mk reads the addresses off the image.
EVAL_LINE_SHIFT = 4
EVAL_LINE_SHIFT_MEASURED = yes

# The accelerator words worth sweeping. THE PACKING IS THIS PART'S OWN, and
# it is the thinnest in the tree: the word is NVMC.ICACHECNF verbatim, with
# CACHEEN at bit 0 and CACHEPROFEN at bit 8. Read mcufw/nrf52840/regs.h, not
# here, for why nothing else is folded into it.
#
#   0x000  cache off. The reset state, and what the part boots into.
#   0x001  cache on.
#   0x101  cache on with the IHIT/IMISS counters running. The counters only
#          increment with bit 8 set, and this is the only word in which
#          ssb_has_monitors reports a counter at all.
#
# EXPECT ALL THREE TO BE INDISTINGUISHABLE, and expect that to be the
# result rather than a broken axis. The cache is on the ICODE bus and the
# research tree proved with these same two counters that it never sees a
# data access: a flash data walk moves neither counter while a code walk
# over the same flash moves both. DATASHEETS.md records the consequence
# directly - "every ICACHECNF setting leaks identically" - and the part
# still gives up the full key, through the read latch.
#
# There is no latency axis to cross these with. This part has no wait-state
# field anywhere, which is the thing that makes it the bound on the claim
# that the channel is about accelerators: it has almost none, and leaks.
EVAL_ACR_WORDS = 0x000 0x001 0x101
