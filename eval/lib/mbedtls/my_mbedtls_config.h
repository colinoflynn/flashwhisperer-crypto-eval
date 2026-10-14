/* Mbed-TLS configuration for the sstimer library victim: AES only, nothing
 * else. Passed with -DMBEDTLS_CONFIG_FILE, which is Mbed-TLS's own documented
 * mechanism - no library source is touched.
 *
 * THE TWO AXES HERE ARE MBED-TLS'S OWN OPTIONS, AND THEIR DEFAULTS MATTER.
 *
 * MBEDTLS_AES_ROM_TABLES defaults to OFF, and that is the stock build: the
 * tables are plain `static` arrays in .bss, filled at run time by
 * aes_gen_tables(), so they live in RAM and the flash read path never sees
 * them. Stock Mbed-TLS therefore has no flash-timing channel at all. Turning
 * it ON is what an integrator does to get roughly 9 KB of RAM back, and it is
 * what puts the tables in flash. The rig builds both and reports which one is
 * running, so the quiet build is a negative control that came with the
 * library rather than one this project invented.
 *
 * MBEDTLS_AES_FEWER_TABLES also defaults to OFF: four 1 KB forward T-tables
 * plus the S-box. ON leaves one T-table, rotated inline, which reintroduces
 * same-line collisions on a smaller footprint.
 *
 * Both are driven from the Makefile (ROMTAB=, FEWER=), not hardcoded here, so
 * the matrix is visible in one place.
 */
#ifndef MY_MBEDTLS_CONFIG_H
#define MY_MBEDTLS_CONFIG_H

#define MBEDTLS_AES_C

#if defined(SS_MBEDTLS_ROM_TABLES)
#define MBEDTLS_AES_ROM_TABLES
#endif

#if defined(SS_MBEDTLS_FEWER_TABLES)
#define MBEDTLS_AES_FEWER_TABLES
#endif

#endif /* MY_MBEDTLS_CONFIG_H */
