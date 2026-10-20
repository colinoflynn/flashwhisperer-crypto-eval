/* Microchip ATSAME51J20A: 1 MB flash at 0x00000000, 256 KB HSRAM at
 * 0x20000000. Both taken from rigs/fw_same51_aes/link.ld in the
 * flash-accelerator-tests tree, which has been built and run on this part.
 *
 * FLASH IS BASED AT ZERO, not at an 0x08000000 alias like the ST parts
 * here. The vector table is therefore the first thing in the image at
 * address zero, which is also where this core boots VTOR.
 *
 * THE ARRAY IS TWO BANKS and NVMCTRL's BKSWRST command swaps which one
 * answers at zero. That is a run-time command, not a link-time decision,
 * so nothing in this file can pin the table's address against it. Nothing
 * in this port issues it, but a capture should still carry the address the
 * device reports rather than one read out of fw.map.
 *
 * WHY THIS RAM REGION IS A VALID TWIN, which is the question the residence
 * control exists to answer and the one a memory map can get wrong without
 * producing an error.
 *
 * The 256 KB at 0x20000000 is one contiguous HSRAM reached by the CPU over
 * the system bus. Neither structure on the flash read path can touch it:
 * the NVMCTRL line caches sit on the AHB ports into the flash array, and
 * the CMCC caches the Cortex-M4 ICode and DCode interfaces, which serve
 * addresses below 0x20000000 and so cannot see this region at all. A table
 * moved here is therefore served by a path with no cache and no read latch
 * on it, which is exactly what the control needs: the twin must differ
 * from the flash arm in the table's residence and in nothing else.
 *
 * That is not only an argument. The research tree measured tables in SRAM
 * on this part as bit-exactly constant in every one of the twenty-two
 * configurations it swept, with zero jitter, which is the twin behaving as
 * a twin must. See research/targets/same51j20a.md in the
 * flash-accelerator-tests tree.
 *
 * There is no CCM, no TCM and no second RAM at a different access path to
 * choose wrongly between, so the decision the F429 and G474 maps had to
 * make does not arise. One caveat worth knowing and not acting on: the
 * first 8 KB of this region is also where the backup-retention and
 * embedded-trace-buffer views land. Nothing here enables either, and the
 * rig puts its host mailbox at 0x20000000 and runs, so the whole region is
 * used.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1024K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
