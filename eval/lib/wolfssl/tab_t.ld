/* wolfSSL forward tables. The default build has Te[4][256] - four 1 KB
   word tables in one array, so ONE section - and WOLFSSL_AES_SMALL_TABLES
   replaces it with Tsbox, which lands in the S-box slot instead. */
*(.rodata.Te)
