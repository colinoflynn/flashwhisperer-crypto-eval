/* STM32F303RE: 512K flash @ 0x08000000, 64K SRAM @ 0x20000000.
 *
 * Both regions are read off rigs/fw_f303_aes/link.ld, which is the
 * script the research tree's F303RE rig is built and run with.
 *
 * WHY SRAM AND NOT THE CCM, WHICH THIS PART ALSO HAS. The RAM region
 * named here is where RAMTAB=1 puts the tables, so it has to be a VALID
 * TWIN: reached by the same path as ordinary data, and not behind
 * anything that could hold a row of its own. SRAM at 0x20000000
 * qualifies on both counts. It is the region .data and .bss already live
 * in under the shared sections script, so the twin's tables are read
 * over exactly the path the victim's other variables are; and this part
 * has no data cache at all - FLASH_ACR here is LATENCY, HLFCYA, PRFTBE
 * and a read-only status bit, with no DCEN and no ART (the research
 * tree's DATASHEETS roster records the F303's data-side structure as
 * "none") - so there is nothing between the core and SRAM to invalidate.
 *
 * THE 16K CCM AT 0x10000000 IS DELIBERATELY EXCLUDED. The part has one
 * (rigs/fw_f303_aes/link.ld declares it, and the rig offers a CCMTAB=1
 * arm that places its RAM tables there), and it is a second RAM on its
 * own bus path. That is precisely what disqualifies it as the twin:
 * moving the tables there would change the access path as well as the
 * residence, so a difference between the two arms would no longer be
 * attributable to the flash. It is an interesting arm in its own right
 * and the rig keeps it as one; it is not this control.
 *
 * The CCM is left out of MEMORY entirely rather than declared and
 * unused, so that a future REGION_ALIAS("TABS", CCM) is a link error
 * naming a region that does not exist, rather than a quiet build that
 * measures something else.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
