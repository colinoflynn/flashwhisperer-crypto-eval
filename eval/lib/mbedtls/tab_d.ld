/* Mbed-TLS reverse tables. mbedtls_aes_crypt_ecb references the decrypt
   path, so these DO get pulled into an encrypt-only image - which is
   realistic, and shows up as a populated third slot. */
*(.rodata.RSb) *(.rodata.RT0) *(.rodata.RT1) *(.rodata.RT2) *(.rodata.RT3)
