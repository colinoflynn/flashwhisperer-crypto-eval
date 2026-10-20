/* One-block software AES through LibTomCrypt's upstream Rijndael API. */
#include <stdint.h>
#include "ss_victim.h"
#include "victim_proto.h"
#include "tomcrypt.h"

static symmetric_key aes;

void prov_init(const uint8_t key[16])
{
    (void)rijndael_setup(key, 16, 0, &aes);
}

void prov_encrypt(const uint8_t in[16], uint8_t out[16])
{
    (void)rijndael_ecb_encrypt(in, out, &aes);
}

void prov_libinfo(ss_libinfo_t *o)
{
    o->lib = SS_LIB_LIBTOMCRYPT;
    o->cfg = ss_common_cfg();
#ifdef LTC_SMALL_CODE
    o->cfg |= SS_LC_FEWER;
#endif
    o->build_byte = 0u;
    /* This pinned develop snapshot has no compile-time semantic version.
     * libs/PINS and the parent gitlink identify the exact source. */
    o->version = 0u;
    o->code = ss_code_addr((const void *)rijndael_ecb_encrypt);
    ss_tables_fill(o);
}
