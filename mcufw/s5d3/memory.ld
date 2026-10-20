/* Renesas Synergy S5D3 (R7FS5D37A3A01CFP): 512 KB code flash based at 0,
 * 256 KB SRAM at 0x1FFE0000.
 *
 * Both origins are copied from the rig this port takes its registers from,
 * rigs/fw_s5d3_aes/link.ld in the research tree, where both were measured on
 * the die rather than decoded from the part number. Neither is the number a
 * reader used to the ST ports in this directory will expect, so both are
 * written out here.
 *
 * WHY FLASH STARTS AT 0x00000500 AND NOT AT 0. The low 0x500 bytes of code
 * flash are not ours. 0x000..0x3FF holds the Cortex-M vector table, and
 * 0x400..0x43F is the Renesas option-setting memory: OFS0 (the two
 * watchdogs), OFS1 (HOCO enable and voltage detection) and the Security MPU
 * region registers. Those words are part of the flash image, they are erased
 * along with it, and writing the wrong value there arms a watchdog or changes
 * the reset clock source rather than failing. Linking ordinary code over them
 * is how that happens by accident, so the region the linker is allowed to
 * fill begins above them.
 *
 * WHY RAM STARTS AT 0x1FFE0000 AND NOT AT 0x20000000. Renesas maps SRAM on
 * this family BELOW the usual Cortex-M SRAM base. The rig records the
 * boundary being probed from both ends: SRAM answers with live data at
 * 0x2001FFFC and returns a repeating constant from 0x20020000 on, and the
 * J-Link device database agrees, R7FS5D37A, RAM 0x1FFE0000, 262144 bytes.
 * A map copied from an RA2A1 or from any part in this directory puts RAM at
 * 0x20000000, which on this die lands the initial stack pointer in space that
 * reads back a constant, and the part does not boot. So a _tabS that comes
 * out at 0x1FFE_xxxx under RAMTAB=1 is the correct answer here, not a broken
 * one.
 *
 * WHAT THIS MAP DOES NOT PRODUCE, which matters before anyone flashes it.
 * The shared sections script (instrumentation/victim/ld/sections.ld) emits
 * .isr_vector into FLASH, so with FLASH based at 0x500 the vector table lands
 * at 0x00000500 and nothing is emitted at 0x00000000 or at the option-setting
 * memory. The core fetches its initial SP and PC from 0x00000000, so this
 * image as linked cannot boot standalone. Giving it those two blocks means a
 * SECTIONS change, and the sections script is shared by every part. Recorded
 * as a known gap rather than worked around here. The port has not been run on
 * silicon.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000500, LENGTH = 512K - 0x500
  RAM  (rwx) : ORIGIN = 0x1FFE0000, LENGTH = 256K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
