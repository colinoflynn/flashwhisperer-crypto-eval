/* Tables relocated into RAM, loaded from flash at startup. Same code, same
   instruction stream, same indices -- only the table's residence differs.

   THE MANDATORY CONTROL. Anything that survives this is the instrument, not
   the flash. It is done at link time rather than in source precisely so it
   is available for a library shipped as a binary archive. */
INCLUDE memory.ld
REGION_ALIAS("TABS", RAM)
INCLUDE sections.ld
