/* STM32F429ZI: 2M flash @ 0x08000000, 192K SRAM.
 *
 * RAM is the 112K SRAM1 at 0x20000000 plus the contiguous 16K SRAM2, so
 * 128K as one region. The 64K CCM at 0x10000000 is NOT included and that
 * is deliberate: CCM is not reachable by every bus master and, more to the
 * point here, putting the RAM table twin somewhere with a different access
 * path would make the twin differ from the flash arm in more than the
 * table's residence -- which is the one thing the control may not do.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2048K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
