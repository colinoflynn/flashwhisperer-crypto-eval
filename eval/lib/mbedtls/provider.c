/* Mbed-TLS. The SHORTEST GLUE THAT WORKS: a key schedule at startup and a
 * one-block ECB encrypt, because everything in prov_encrypt runs inside the
 * measured window and the point is to measure the LIBRARY, not the wrapper.
 *
 * Nothing here configures Mbed-TLS. The two axes are its OWN options, set
 * through MBEDTLS_CONFIG_FILE from lib.mk -- see my_mbedtls_config.h.
 */
#include <stdint.h>
#include "ss_victim.h"
#include "victim_proto.h"
#include "mbedtls/aes.h"
#include "mbedtls/build_info.h"

static mbedtls_aes_context ctx;

void prov_init(const uint8_t key[16])
{
    mbedtls_aes_init(&ctx);
    (void)mbedtls_aes_setkey_enc(&ctx, key, 128);
}

void prov_encrypt(const uint8_t in[16], uint8_t out[16])
{
    (void)mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
}

void prov_libinfo(ss_libinfo_t *o)
{
    o->lib = SS_LIB_MBEDTLS;
    o->cfg = ss_common_cfg();
    /* STOCK MBED-TLS HAS MBEDTLS_AES_ROM_TABLES OFF: the tables are plain
     * `static` arrays in .bss, generated at run time by aes_gen_tables(),
     * and therefore in RAM. So the bit being CLEAR is the default build,
     * and that build is EXPECTED TO BE QUIET -- it is a negative control
     * that came with the library rather than one this project invented.
     * Turning it on is what an integrator does to get ~9 KB of RAM back,
     * and it is the configuration that puts the tables in flash.
     *
     * Reported from the macro rather than assumed from the Makefile,
     * because the directory name is what someone meant to build and this is
     * what is running. */
#if defined(MBEDTLS_AES_ROM_TABLES)
    o->cfg |= SS_LC_ROMTAB;
#endif
#if defined(MBEDTLS_AES_FEWER_TABLES)
    o->cfg |= SS_LC_FEWER;
#endif
    o->build_byte = 0u;
    o->version = ((uint32_t)MBEDTLS_VERSION_MAJOR << 16)
               | ((uint32_t)MBEDTLS_VERSION_MINOR << 8)
               | (uint32_t)MBEDTLS_VERSION_PATCH;
    o->code = ss_code_addr((const void *)mbedtls_aes_crypt_ecb);
    ss_tables_fill(o);
}
