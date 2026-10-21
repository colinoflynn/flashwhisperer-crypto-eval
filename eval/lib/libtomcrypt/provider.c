/* One-block software AES through LibTomCrypt's upstream Rijndael API. */
#include <stdint.h>
#include "ss_victim.h"
#include "victim_proto.h"
#include "tomcrypt.h"

/* NOT `symmetric_key', WHICH IS A UNION OVER EVERY CIPHER LIBTOMCRYPT HAS.
 * tomcrypt_custom.h enables all of them by default, so that union is sized
 * by the largest - 4 256 bytes of .bss to hold a 508-byte AES schedule.
 * Only Rijndael is compiled here (see lib.mk's LIB_SRC), so the other
 * members are storage for ciphers that are not in the image.
 *
 * IT IS NOT A SIZE OPTIMISATION. On the two parts with 16 KB of SRAM it is
 * the difference between the RAMTAB=1 twin linking and overflowing the
 * region, and the twin is the control that makes the flash arm readable.
 *
 * WHY NOT -DLTC_NOTHING -DLTC_RIJNDAEL, which is upstream's own switch for
 * exactly this. It works and gives the same 508 bytes, but it also makes
 * rijndael_setup 40 bytes shorter, which moves memcpy, which changes the
 * eight `bl` displacements inside rijndael_ecb_encrypt. Doing it here
 * instead leaves upstream's compilation alone.
 *
 * WHAT WAS CHECKED, on the F429's three libtomcrypt builds, against the
 * images from before this change:
 *
 *   - .text and .aestab identical in size, and NO FLASH SYMBOL MOVED.
 *   - rijndael_ecb_encrypt bit-identical, still at 0x08003a8c.
 *   - .aestab bit-identical, still at 0x08000400 (0x20000000 for the twin).
 *
 * The images are NOT identical and the claim is not that they are: five
 * bytes differ, across four literals holding the address of this object -
 * one each in prov_init and prov_encrypt, and two in the pool that
 * rijndael_ecb_encrypt reads from - because that address changed. It
 * is a RAM address reached once per call either way, and it is not on the
 * flash read path the captures measure. That matters because
 * results/stm32f429zi/README.md files those captures against the cipher's
 * address and the table's, and both are unchanged.
 *
 * The cast is to the union the upstream API takes. Every member of a union
 * starts at offset 0 and aes.c touches only `skey->rijndael`, so the only
 * bytes it can reach are the ones declared here. */
static union { struct rijndael_key rijndael; } aes;

void prov_init(const uint8_t key[16])
{
    (void)rijndael_setup(key, 16, 0, (symmetric_key *)&aes);
}

void prov_encrypt(const uint8_t in[16], uint8_t out[16])
{
    (void)rijndael_ecb_encrypt(in, out, (symmetric_key *)&aes);
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
