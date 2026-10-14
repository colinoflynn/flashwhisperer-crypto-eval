/* STM32L152RE: 512K flash @ 0x08000000, 80K SRAM @ 0x20000000.
 *
 * A PART PROPERTY, and the only linker file a port has to write. The
 * SECTIONS block is shared (instrumentation/victim/ld/sections.ld) and the
 * table input-section names belong to the library
 * (eval/lib/<name>/tab_[tsd].ld).
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 80K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
