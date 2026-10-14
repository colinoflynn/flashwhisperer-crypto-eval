/* Tables stay in flash, where the library's own build put them. This is the
   configuration under test. */
INCLUDE memory.ld
REGION_ALIAS("TABS", FLASH)
INCLUDE sections.ld
