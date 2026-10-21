/* Infineon XMC1100-Q024F0064: 64 KB flash, 16 KB SRAM, Cortex-M0.
 *
 * A PART PROPERTY, and the only linker file a port writes. The SECTIONS
 * block is shared (instrumentation/victim/ld/sections.ld) and the table
 * input-section names belong to the library (eval/lib/<name>/tab_[tsd].ld).
 *
 * BOTH ORIGINS HERE ARE OFF THE BASE OF THEIR REGION, and both are XMC1000
 * quirks that no other part in this tree has. Neither is a tidy-up and
 * neither may be normalised.
 *
 * 1. USER FLASH STARTS AT 0x10001000, NOT AT THE BASE OF THE FLASH REGION.
 *    0x10000000..0x10000FFF is the flash configuration sector: the BMI
 *    (boot mode index), the SSW clock words CLK_VAL1 and CLK_VAL2, and the
 *    protection words. The part boots from an 8 KB ROM at 0x00000000 which
 *    reads the initial stack pointer from 0x10001000 and the reset vector
 *    from 0x10001004, so .isr_vector landing exactly at this ORIGIN is what
 *    makes the image bootable. The research rig this port's registers come
 *    from (rigs/fw_xmc1_aes/link.ld) confirmed the layout by reading the
 *    factory image off this die: 0x20000920 and 0x10001019 in those two
 *    words. Nothing in the image addresses the configuration sector, so it
 *    is never in a write range; erasing it can leave a part that does not
 *    answer SWD.
 *
 * 2. SRAM USABLE BY THIS IMAGE STARTS AT 0x20000100, NOT AT 0x20000000.
 *    Cortex-M0 has no VTOR, so XMC1000 fixes the exception table in ROM and
 *    remaps every vector to a stub the application must leave room for at
 *    0x2000000C..0x200000BF. The shared sections script knows nothing about
 *    that reservation and would put .data, .bss or a relocated table on top
 *    of it, so the region is excluded here, which is the only place a port
 *    can exclude it. 0x100 rather than 0xC0 keeps the first usable address
 *    on a round boundary, which is also where the rig put its own mailbox.
 *
 *    A fault during bring-up would then vector into live measurement data
 *    rather than into a reserved hole. That is a debugging trap rather than
 *    a correctness bug, which is the kind that costs a day.
 *
 * WHY THIS RAM REGION IS A VALID TWIN for the RAMTAB=1 control. Three
 * things have to hold, and they do:
 *
 *   - It is ordinary SRAM on the core's own bus with no NVM controller in
 *     the path, so moving a table here removes the flash read path and
 *     nothing else. There is no cache anywhere on this die to also remove -
 *     research/targets/xmc1100.md measured the data side as having no
 *     structure at all - so the twin differs from the flash arm in exactly
 *     one thing.
 *   - The table lands at the SAME ALIGNMENT in both arms. sections.ld
 *     starts .aestab with ALIGN(1024), and from ORIGIN 0x20000100 that puts
 *     _tabstart at 0x20000400, so the low address bits the row arithmetic
 *     is written against are identical to the flash arm's.
 *   - It is below _estack and leaves the ASSERT in sections.ld its 4 KB of
 *     stack headroom FOR A LIBRARY WHOSE TABLES FIT, which on 16 KB of SRAM
 *     is the thing most likely to fail. Measured on the built image rather
 *     than estimated: refsbox LAYOUT=sbox RAMTAB=1 puts the tables at
 *     0x20000400..0x200014FF (4 352 bytes - the four 1 KB T-tables are
 *     linked in whatever the layout, plus the 256-byte S-box), then .data
 *     and .bss to 0x200026C8, leaving 6 456 bytes of stack below _estack
 *     at 0x20004000. RAMTAB=0 ends at 0x200012C8 and leaves 11 576. The
 *     768 bytes between ORIGIN and the aligned table start are the cost of
 *     reserving the vector region, and the part has them to spare.
 *
 *     THIS WAS WRITTEN AS "with the whole table set resident" AND THAT WAS
 *     WRONG - it generalised a refsbox measurement to every library. The
 *     bound is LibTomCrypt's default configuration: twelve 1 KB tables that
 *     survive garbage collection, leaving 2 560 bytes where the assertion
 *     wants 4 096. board.mk declares that cell in BOARD_NO_CELLS. What this
 *     region holds is a twin of up to about 8 KB of tables, not any twin.
 *
 * What is NOT established: whether the boot ROM or the SSW uses any SRAM
 * above 0xBF while user code runs. The rig reserves 0xC0 and nothing more,
 * and that rig has run on this die, which is the evidence this file has.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x10001000, LENGTH = 64K - 0x1000
  RAM  (rwx) : ORIGIN = 0x20000100, LENGTH = 16K - 0x100
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
