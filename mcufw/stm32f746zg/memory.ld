/* STM32F746ZG: 1M flash @ 0x08000000, and DTCM as the RAM.
 *
 * RAM IS DTCM, 64K AT 0x20000000, for the same reason as the H723 and
 * with a sharper edge on this family. SRAM1 at 0x20010000 is behind the
 * L1 D-cache, so a table relocated there is still read through a cache
 * and the twin would be measuring the cache rather than being a control.
 *
 * And the boundary between them MOVES WITHIN THE FAMILY: on an F746 the
 * DTCM is 64K, so 0x20010000 is cacheable SRAM1; on an F769 the DTCM is
 * 128K and that same address is DTCM. A memory map copied between two
 * parts of one family is how a control quietly stops being one, which is
 * why this file names the part in its first line and why SRAM1 is left
 * out rather than merged in to make the region look bigger.
 *
 * 64K is ample here: the victim's text is in flash and only the stack,
 * .data, .bss and the 1.3K table twin live in RAM.
 *
 * NOTE the DTCM twin on this part is NOT expected to be bit-exactly
 * constant the way the L152's and H723's are. DTCM is not on the same
 * fabric as the flash, and the research tree measured `flash - twin =
 * +6.000` cycles here with a twin sigma of 0.003 over two values. A twin
 * whose spread is not zero is still a valid control if its RECOVERY is
 * null - a statistic is not a channel, applied to the control.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
