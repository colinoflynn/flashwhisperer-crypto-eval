/* ATSAMD21J18A: 256K flash @ 0x00000000, 32K SRAM @ 0x20000000.
 *
 * A PART PROPERTY, and the only linker file a port has to write. The SECTIONS
 * block is shared (instrumentation/victim/ld/sections.ld) and the table input
 * section names belong to the library (eval/lib/<name>/tab_[tsd].ld).
 *
 * FLASH IS BASED AT ZERO, unlike every ST part in this tree. There is no
 * configuration sector below the application to step over either: the SAM
 * D21's equivalent words, the NVM user row (BOOTPROT, EEPROM, BOD, WDT) and
 * the factory calibration row, live in a separate auxiliary address space at
 * 0x00804000 and 0x00806000 which is not part of the 256 KB main array. The
 * board file reads the DFLL coarse calibration out of 0x00806024; nothing
 * here writes any of it.
 *
 * Sizes are the vendor header's, rigs/cmsis/samd21/samd21j18a.h:
 * FLASH_SIZE 0x00040000 at FLASH_ADDR 0x00000000, HMCRAMC0_SIZE 0x00008000
 * at HMCRAMC0_ADDR 0x20000000.
 *
 * WHY THIS RAM IS A VALID TWIN for the table-residence control, which is the
 * question worth asking of any part before its RAM arm means anything:
 *
 *   There is exactly one RAM to choose from. HMCRAMC0 is the whole of this
 *   part's SRAM, a single 32 KB block at 0x20000000, so there is no second
 *   region with different timing that the choice could silently land in.
 *
 *   It is off the measured path by construction. The structure under test is
 *   the NVM cache, which lives in NVMCTRL on the flash read path; an SRAM
 *   read never reaches NVMCTRL at all. So moving the tables here removes the
 *   table's data allocations from the cache and leaves the code's instruction
 *   fetches going through it, which is exactly the control the twin is for.
 *
 *   Nothing on the RAM path holds a row. The part has no data cache, no write
 *   buffer and no wait states on SRAM, so a twin that still varies in time is
 *   evidence about the instrument rather than about the memory.
 *
 *   It fits with headroom. The widest layout this tree builds is four 1 KB
 *   T-tables plus a 256-byte S-box, under 4.5 KB against 32 KB, leaving the
 *   stack far more than the 4 KB that sections.ld asserts for.
 *
 * None of this has been checked on silicon. It is the reasoning for the
 * choice, not a measurement of it; step 6 of docs/PORTING.md is what would
 * settle whether the twin really is bit-exactly constant here.
 */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Exported so startup.c can ask whether the table section landed in RAM
 * without a literal address of its own. See the note on the table copy loop
 * there: on this part the usual `_tabload != _tabstart` test is not safe. */
_ram_origin = ORIGIN(RAM);
