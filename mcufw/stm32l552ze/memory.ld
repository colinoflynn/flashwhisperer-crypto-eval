/* STM32L552ZE: 512K flash at 0x08000000, 256K SRAM at 0x20000000.
 *
 * WHICH ALIAS. This part is TrustZone-capable, so the flash and the SRAM
 * each appear twice: non-secure at 0x08000000 and 0x20000000, secure at
 * 0x0C000000 and 0x30000000. This map uses the NON-SECURE aliases,
 * because that is the memory system the part was measured in. The three
 * L5 rigs in the research repository all build for TZEN = 0, link at
 * 0x08000000 / 0x20000000 and set VTOR to 0x08000000
 * (flash-accelerator-tests/rigs/fw_l5_cache/link.ld and startup.c), and
 * every L552 result in that tree was taken that way. A secure-alias build
 * is a different experiment, not a cosmetic change: the aliases are
 * separate ICACHE regions and the comparison with the existing numbers
 * would no longer be a comparison.
 *
 * WHY THIS RAM IS A VALID TWIN, which on this part is a sharper question
 * than on the others. The ICACHE here is UNIFIED and covers the whole
 * code region over the Cortex-M33 C-AHB port, so a table left in flash is
 * read through the cache and through the two-entry flash read latch under
 * it. SRAM at 0x20000000 is on the S-AHB port and never passes through
 * the ICACHE at all. So moving the table here takes it out of BOTH
 * structures and out of nothing else: same code, same instruction stream,
 * same indices. That is exactly what the control may do and no more, and
 * it is why the rigs run their timing loops from SRAM
 * (flash-accelerator-tests/rigs/fw_l5_cache/link.ld, and section 1 of
 * research/targets/stm32l552.md).
 *
 * The 256K is SRAM1's 192K at 0x20000000 followed by SRAM2's 64K at
 * 0x20030000, which are contiguous, so there is no second region to
 * choose between and no F429-style question about which one the twin
 * should live in. The bases and sizes are the CMSIS header's
 * (SRAM1_BASE_NS, SRAM2_BASE_NS) and the single 256K region is the one
 * the rigs link against.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
