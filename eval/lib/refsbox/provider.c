/* refsbox -- this project's own byte S-box AES, and THE CONTROL ON THE RIG
 * ITSELF.
 *
 * The only "library" here whose source is in the tree rather than in libs/,
 * because it is not a library under test: it is the known quantity. Before
 * any third-party result on any part is worth reading, this provider has to
 * reproduce the baseline already measured for that part -- same table
 * address, same spread. If it does not, the instrument moved, not the
 * library.
 *
 * It is also the one provider with more than one table layout, so it is what
 * a new part's porting checklist exercises first: 's' byte S-box, 't' four
 * word T-tables, 'c' one rotated T-table.
 */
#include <stdint.h>
#include "ss_victim.h"
#include "victim_proto.h"
#include "aes.h"

static uint32_t rk[44];

void prov_init(const uint8_t key[16])
{
    /* aes_use_ram(0) ALWAYS. This cipher has its own RAM-table switch, which
     * flips a POINTER the cipher reads through -- so the flash and RAM arms
     * would differ in their instruction stream as well as in the table's
     * address. This repository uses the LINKER instead, which moves only the
     * data: same code, same instruction stream, same indices. That is the
     * control that is actually wanted, and it is the one axis that works the
     * same way for every library here including a binary archive.
     *
     * The call is left in and set to 0 rather than removed, so that the
     * pointer indirection is present in both arms and cannot be the thing
     * that differs. */
    aes_init_ram_tables();
    aes_use_ram(0);
    aes_key_schedule(key, rk);
}

void prov_encrypt(const uint8_t in[16], uint8_t out[16])
{
#if SS_LAYOUT == 't'
    aes_ttable_encrypt(in, out, rk);
#elif SS_LAYOUT == 'c'
    aes_ttable1_encrypt_nr(in, out, rk, 10);
#else
    aes_sbox_encrypt(in, out, rk);
#endif
}

void prov_libinfo(ss_libinfo_t *o)
{
    o->lib = SS_LIB_REFSBOX;
    o->cfg = ss_common_cfg();
    o->build_byte = 0u;
    o->version = 0u;                 /* this project's own code, unreleased */
#if SS_LAYOUT == 't'
    o->code = ss_code_addr((const void *)aes_ttable_encrypt);
#elif SS_LAYOUT == 'c'
    o->code = ss_code_addr((const void *)aes_ttable1_encrypt_nr);
#else
    o->code = ss_code_addr((const void *)aes_sbox_encrypt);
#endif
    ss_tables_fill(o);
}
