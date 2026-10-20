/* Renesas RA2A1 (R7FA2A1AB3CFM): 256K code flash at 0, 32K SRAM at 0x20000000.
 *
 * A PART PROPERTY. The SECTIONS block is shared
 * (instrumentation/victim/ld/sections.ld) and the table input-section names
 * belong to the library (eval/lib/<name>/tab_[tsd].ld).
 *
 * THE CODE REGION DOES NOT START AT ZERO, AND THAT IS NOT A SIMPLIFICATION
 * WAITING TO HAPPEN. The low 0x500 bytes of this part's flash are not
 * ordinary code space:
 *
 *   0x000..0x3FF   the vector table the core fetches at reset
 *   0x400..0x43F   the OPTION-SETTING MEMORY: OFS0 (watchdog), OFS1 (HOCO
 *                  enable and voltage detect), and the Security MPU region
 *                  registers. It is flash, it is erased together with the
 *                  block that holds the vector table whenever a programmer
 *                  writes address 0, and if an image does not re-emit it the
 *                  words come back 0xFFFFFFFF, which stops HOCO and boots the
 *                  part on MOCO instead.
 *   0x440..0x4FF   reserved, left out of both regions
 *
 * So the layout below is the one rigs/fw_ra2_aes/link.ld uses, carved into
 * three regions for the three different things that live there, and the
 * values re-emitted into OPT come from that rig's startup.c where they were
 * read off this die before its first flash.
 *
 * THE TWO OUTPUT SECTIONS BELOW ARE THE ONE PLACE THIS PORT DEVIATES from the
 * "memory.ld holds only MEMORY, ENTRY and _estack" division. They have to be
 * here because the shared sections.ld sends .isr_vector to FLASH, and on this
 * part FLASH starts at 0x500: a vector table placed there would never be
 * fetched. ld merges SECTIONS commands in the order it reads them, and this
 * file is included first, so these two land before anything shared and the
 * shared .isr_vector stays empty and harmless.
 */
ENTRY(Reset_Handler)

MEMORY
{
  VECT  (rx)  : ORIGIN = 0x00000000, LENGTH = 0x400
  OPT   (r)   : ORIGIN = 0x00000400, LENGTH = 0x40
  FLASH (rx)  : ORIGIN = 0x00000500, LENGTH = 256K - 0x500
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

SECTIONS
{
  .vectors        : { KEEP(*(.vectors))        } > VECT
  .option_setting : { KEEP(*(.option_setting)) } > OPT
}

/* THE RAM REGION IS A VALID TWIN, which is the only question that matters
 * about it here, because RAMTAB=1 aliases TABS to RAM and the whole residence
 * control rests on the two arms differing in the table's memory and in
 * nothing else.
 *
 * This part has ONE SRAM: 32K at 0x20000000, with no second bank, no TCM, no
 * cacheable and non-cacheable alias of the same array, and no backup domain
 * that would answer at a different speed. There is therefore no way to pick
 * the wrong RAM here, which is not true of every part on the bench.
 *
 * It is also the same memory the published RA2A1 figures are normalised
 * against: the rig's SRAM twin, PROBE_RAM in rigs/fw_ra2_aes/main.c, is a
 * plain .bss array in this region, and research/targets/ra2a1.md's pair probe
 * reads it flat at 5.000 cycles per read at every gap while the flash arm
 * splits 5.000 hit against 7.000 miss. So a table relocated here is in the
 * memory that has already been shown to carry no gap-dependent structure on
 * this die, which is what a twin has to be.
 *
 * Headroom: the victim's tables are at most about 4.25K and .bss is small, so
 * the 4K stack assertion in sections.ld has room. That assertion is the check,
 * not this comment.
 */
_estack = ORIGIN(RAM) + LENGTH(RAM);
