/* wolfSSL. The SHORTEST GLUE THAT WORKS: a key schedule at startup and
 * wc_AesEncryptDirect for one block.
 *
 * Nothing here configures wolfSSL. Its one table axis is its OWN option,
 * set through WOLFSSL_USER_SETTINGS from lib.mk - see user_settings.h.
 */
#include <stdint.h>
#include "ss_victim.h"
#include "victim_proto.h"
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/version.h>
#include <wolfssl/wolfcrypt/aes.h>

static Aes aes;

void prov_init(const uint8_t key[16])
{
    (void)wc_AesInit(&aes, NULL, INVALID_DEVID);
    (void)wc_AesSetKey(&aes, key, 16, NULL, AES_ENCRYPTION);
}

void prov_encrypt(const uint8_t in[16], uint8_t out[16])
{
    (void)wc_AesEncryptDirect(&aes, out, in);
}

void prov_libinfo(ss_libinfo_t *o)
{
    o->lib = SS_LIB_WOLFSSL;
    o->cfg = ss_common_cfg();
    /* wolfSSL HAS NO ROM/RAM SWITCH: Te and Td are `static const` and live
     * in flash in EVERY build, so there's no equivalent of Mbed-TLS's
     * ROM_TABLES bit, and the link-time twin is the only residence axis it
     * has. WOLFSSL_AES_SMALL_TABLES defaults OFF (wolfSSL's own options
     * comment in aes.c says so), and turning it on replaces the four 1 KB
     * word tables with a 256-byte Tsbox. */
#if defined(WOLFSSL_AES_SMALL_TABLES)
    o->cfg |= SS_LC_FEWER;
#endif
    o->build_byte = 0u;
    /* LIBWOLFSSL_VERSION_HEX packs 5.9.4 as 0x05009004: major in the top
     * byte, then TWELVE-bit minor and patch fields. Reading it as three
     * bytes gives "5.0.144", which is how this was noticed. */
    o->version = ((uint32_t)((LIBWOLFSSL_VERSION_HEX >> 24) & 0xFFu) << 16)
               | ((uint32_t)((LIBWOLFSSL_VERSION_HEX >> 12) & 0xFFFu) << 8)
               | (uint32_t)(LIBWOLFSSL_VERSION_HEX & 0xFFFu);
    o->code = ss_code_addr((const void *)wc_AesEncryptDirect);
    ss_tables_fill(o);
}
