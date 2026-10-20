/* All forward encryption tables are 32-bit words, including the last-round
   S-box-derived Te4 tables. There is no separate 256-byte S-box slot. */
*(.rodata.TE0) *(.rodata.TE1) *(.rodata.TE2) *(.rodata.TE3)
*(.rodata.Te4) *(.rodata.Te4_0) *(.rodata.Te4_1)
*(.rodata.Te4_2) *(.rodata.Te4_3)
