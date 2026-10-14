/* STM32G474RE: 512K flash @ 0x08000000, SRAM1+SRAM2 @ 0x20000000.
 *
 * RAM is the 80K SRAM1 plus the contiguous 16K SRAM2, so 96K as one
 * region. The 32K CCM at 0x10000000 is deliberately NOT in here, for the
 * same reason as on the F429: CCM is reached by a different path, so a
 * table twin placed there would differ from the flash arm in more than
 * the table's residence - which is the one thing the control may not do.
 *
 * Worth knowing for this part specifically: the research tree measured
 * tables in SRAM as not merely the safe placement but the FASTEST one,
 * so the twin here is not a performance sacrifice.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
