/* STM32L475VG: 1024K flash @ 0x08000000, 96K SRAM1 @ 0x20000000.
 *
 * Both regions are read off rigs/fw_l4_aes/link.ld, which is the script
 * the research tree's L475VG rig is built and run with.
 *
 * A PART PROPERTY, and the only linker file a port has to write. The
 * SECTIONS block is shared (instrumentation/victim/ld/sections.ld) and
 * the table input-section names belong to the library
 * (eval/lib/<name>/tab_[tsd].ld).
 *
 * THE 32K SRAM2 IS DELIBERATELY NOT IN HERE. It lives at 0x10000000 with
 * an alias at 0x20018000, and it is the region ST's own bring-up code
 * hands the stack. The RAM region named here is where RAMTAB=1 puts the
 * tables, so it has to be a VALID TWIN - reached by the same path as
 * ordinary data, and differing from the flash arm in the table's
 * RESIDENCE and in nothing else. SRAM1 qualifies: it is the region .data
 * and .bss already live in under the shared sections script, so the
 * twin's tables are read over exactly the path the victim's other
 * variables are. SRAM2 is a perfectly good SECOND twin but a bad
 * DEFAULT, because a table that lands there by accident is a table whose
 * region nobody recorded - so SRAM1 only, and `RAM` means exactly one
 * address range.
 *
 * This part needs none of the M7 parts' care about which RAM: there is
 * no Cortex-M4 L1 cache in front of either region here, so an ordinary
 * SRAM twin is a real twin. _PROCEDURE.md section 4's DTCM rule is an M7
 * rule and does not apply.
 *
 * WHY THE TWIN MATTERS MORE ON THIS PART THAN ON MOST. The research tree
 * measures tables in SRAM1 as the ONLY complete fix for this die - a
 * deep RAM capture recovers 0/16 at margin exactly 0.000 - while turning
 * the accelerator off is a discount for the attacker rather than a fix.
 * So the RAM arm here is the headline result and not only the control
 * that makes the flash arm readable.
 *
 * SRAM2 is left out of MEMORY entirely rather than declared and unused,
 * so that a future REGION_ALIAS("TABS", SRAM2) is a link error naming a
 * region that does not exist, rather than a quiet build that measures
 * something else.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
