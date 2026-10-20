/* The unmodified AES source also builds a decrypt schedule at setup. Move
   these const tables with the forward tables for the RAM-residence twin. */
*(.rodata.TD0) *(.rodata.TD1) *(.rodata.TD2) *(.rodata.TD3)
*(.rodata.Td4) *(.rodata.Tks0) *(.rodata.Tks1)
*(.rodata.Tks2) *(.rodata.Tks3)
