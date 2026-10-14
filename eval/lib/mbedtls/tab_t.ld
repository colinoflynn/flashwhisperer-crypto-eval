/* Mbed-TLS forward T-tables, four 1 KB word tables. Present only when
   MBEDTLS_AES_ROM_TABLES is on; stock Mbed-TLS generates them into .bss at
   run time, so these match nothing and the slot reports address 0. */
*(.rodata.FT0) *(.rodata.FT1) *(.rodata.FT2) *(.rodata.FT3)
