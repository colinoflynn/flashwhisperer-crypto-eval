/* Microchip ATSAMG55J19: 512K flash at 0x00400000, 160K SRAM at 0x20000000.
 *
 * THE FLASH IS AT 0x00400000, WHICH IS NEITHER 0 NOR 0x08000000. SAM3 and
 * SAM4 descendants, and the SAM G55 with them, put the internal flash array
 * at 0x00400000 and mirror whatever GPNVM1 selects, boot ROM or flash, into
 * an alias at 0x00000000 from which the core fetches its initial SP and PC.
 * So an image linked at 0 appears to work through the alias on a part that
 * is already set to boot from flash, and a .bin written at 0 programs
 * nothing at all. The research tree's rig links at 0x00400000 and
 * hardware/boards.py carries base=0x00400000 for this board; this map
 * reproduces that, and startup.c writes VTOR explicitly rather than relying
 * on the mirror.
 *
 * THE RAM REGION IS 160K BECAUSE THE PART HEADER SAYS SO, not because the
 * product brief does. IRAM_SIZE in the vendor header is 0x28000 while the
 * brief says 176 KB, and an over-long region puts _estack somewhere that
 * does not answer, which faults before main rather than reporting a size
 * mistake. 0x28000 is what the rig gives its linker and is what is used
 * here.
 *
 * WHICH RAM THE TABLE TWIN GOES IN: there is only one choice, and that is
 * worth saying rather than leaving implied. This part has a single
 * contiguous 160 KB SRAM and no second bank, no CCM, no TCM and no
 * separately clocked region, so the question the F429 map has to answer
 * does not arise - the RAMTAB=1 twin, the stack, .data and .bss all live in
 * the same array, reached over the same bus, and nothing about the twin's
 * placement is a variable. 160 KB against a 4.25 KB worst-case table set
 * also means the twin never competes for space.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00400000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 160K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
