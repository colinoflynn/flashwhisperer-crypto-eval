/* STM32H723ZG: 1M flash @ 0x08000000, and DTCM as the RAM.
 *
 * RAM IS DTCM, 128K AT 0x20000000, AND THAT IS THE WHOLE POINT ON THIS
 * PART. The residence control needs a RAM that the table can move to
 * without acquiring a different timing channel on the way. On an M7 most
 * of the RAM is not that: AXI SRAM at 0x24000000 sits behind the L1 data
 * cache, so a table relocated there is still read through a cache and the
 * twin would be measuring a cache rather than being a control. DTCM is
 * single-cycle and is not behind the D-cache, so it is the only true
 * uncached twin the part offers.
 *
 * AXI SRAM is deliberately not in this map. If someone wants the
 * cached-RAM arm - which is a real and interesting third case, not a
 * mistake - it should be its own axis with its own name, not something the
 * linker falls into because a region was big enough.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
