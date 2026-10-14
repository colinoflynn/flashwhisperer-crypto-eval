/* STM32F401RE: 512K flash @ 0x08000000, 96K SRAM @ 0x20000000.
 *
 * One contiguous SRAM region and no CCM on this part, so the choice the
 * F429's map had to make does not arise here. Everything the twin needs
 * is in the same 96K the stack and .bss use.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
