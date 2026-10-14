/* The provider contract and the victim entry point.
 *
 * The board half is ss_board.h; the wire protocol is
 * instrumentation/protocol/victim_proto.h. This file is the half a LIBRARY
 * under test has to fill in, and it's deliberately three functions: a key
 * schedule, a one-block encrypt, and a description of where that library's
 * tables ended up.
 *
 * Everything in prov_encrypt runs inside the measured window, so a provider
 * is the SHORTEST GLUE THAT WORKS. Nothing in a provider configures its
 * library: that's done entirely through the library's own documented
 * configuration mechanism, from eval/lib/<name>/lib.mk. Not one line of
 * library source is modified anywhere in this repository, and `make verify`
 * checks it rather than asserting it.
 */
#ifndef SS_VICTIM_H
#define SS_VICTIM_H

#include <stdint.h>

/* What SS_V_LIBINFO reports. Filled by the provider, since only it knows what
 * its library is and what it was configured with. But the TABLE ADDRESSES
 * come from the linker, via ss_tables_fill() below, because the tables are
 * file-static in most libraries and inside a prebuilt archive in at least
 * one, so they aren't addressable from C at all. */
typedef struct
{
    uint8_t  lib;         /* SS_LIB_*                                       */
    uint8_t  cfg;         /* SS_LC_* bitmap                                 */
    uint8_t  n_tab;       /* how many of tab[] are meaningful               */
    uint8_t  build_byte;  /* the library's own build byte, 0 if it has none */
    uint32_t tab[3];      /* table runtime addresses, 0 = not in flash      */
    uint32_t code;        /* the encrypt entry point                        */
    uint32_t version;     /* (major << 16) | (minor << 8) | patch           */
} ss_libinfo_t;

/* ---- the provider (the library under test) supplies these -------------- */
void     prov_init(const uint8_t key[16]);
void     prov_encrypt(const uint8_t in[16], uint8_t out[16]);
void     prov_libinfo(ss_libinfo_t *out);

/* ---- helpers the providers share --------------------------------------- */
/* Fill tab[] and n_tab from the three linker-exported slots. Every provider
 * calls this rather than naming its library's tables, which is what makes
 * the residence axis work identically for a source library and a binary
 * archive. The slots are defined in eval/lib/<name>/tables.ld. */
void     ss_tables_fill(ss_libinfo_t *o);
/* A function pointer carries the Thumb bit on Cortex-M, and the ADDRESS is
 * what a capture wants, so mask it. */
uint32_t ss_code_addr(const void *fn);
/* The configuration bits that aren't the library's own. Currently just the
 * link-time table residence. */
uint8_t  ss_common_cfg(void);

/* ---- and this is the whole victim -------------------------------------- */
void     ssv_run(const uint8_t key[16]);   /* never returns */

#endif /* SS_VICTIM_H */
