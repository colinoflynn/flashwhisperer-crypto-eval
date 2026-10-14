/* The victim protocol loop. SHARED ACROSS PARTS AND LIBRARIES, built
 * unmodified for every combination of them.
 *
 * The part-specific half is behind the ssb_* hooks (ss_board.h), the
 * library-specific half behind the prov_* hooks (ss_victim.h), and nothing
 * in this file names either a part or a library.
 *
 * See instrumentation/protocol/victim_proto.h for the wire format and why it
 * carries no ASCII.
 */
#include "ss_victim.h"
#include "ss_board.h"
#include "victim_proto.h"

#ifndef SS_WARMUP
#define SS_WARMUP 1
#endif

static uint8_t g_trig_on = 1u;

/* ---- the slots the linker fills ---------------------------------------
 * Bounds exported by eval/lib/<name>/tables.ld through the shared sections
 * script. A slot with zero length means this configuration has no table of
 * that kind -- wolfSSL with SMALL_TABLES has no Te, Mbed-TLS with
 * FEWER_TABLES has only FT0, and stock Mbed-TLS has none of the three
 * because it generates its tables into .bss at run time. */
extern const char _tabS[], _tabS_end[];   /* the byte S-box          */
extern const char _tabT[], _tabT_end[];   /* the 1 KB word T-tables  */
extern const char _tabD[], _tabD_end[];   /* decryption tables       */

static uint32_t slot(const char *a, const char *b)
{
    return (b > a) ? (uint32_t)(uintptr_t)a : 0u;
}

void ss_tables_fill(ss_libinfo_t *o)
{
    /* Slot 0 is the S-box because that is the table almost every
     * configuration has at least one of, so it is the useful "primary"
     * address for SS_V_IDENT. The physical order in the image is T-tables
     * first; see the sections script. */
    o->tab[0] = slot(_tabS, _tabS_end);
    o->tab[1] = slot(_tabT, _tabT_end);
    o->tab[2] = slot(_tabD, _tabD_end);
    o->n_tab = (uint8_t)((o->tab[0] ? 1 : 0) + (o->tab[1] ? 1 : 0)
                         + (o->tab[2] ? 1 : 0));
}

uint32_t ss_code_addr(const void *fn)
{
    return (uint32_t)(uintptr_t)fn & ~1u;
}

uint8_t ss_common_cfg(void)
{
    uint8_t c = 0u;
#if SS_RAMTAB
    c |= SS_LC_RAMTAB;
#endif
    return c;
}

/* ---- the measured region, and the ONLY place the trigger moves ---------
 *
 * The warm-up (SS_WARMUP, default on) runs a FIXED plaintext first, so trace
 * k does not depend on trace k-1's plaintext through the table-line state.
 * It is OUTSIDE the trigger window and INSIDE the UART interval, which is
 * most of why the externally timed turnaround runs about twice the trigger
 * pulse width.
 */
static void encrypt_measured(const uint8_t in[16], uint8_t out[16],
                             uint32_t *cyc)
{
#if SS_WARMUP
    static const uint8_t ZERO[16] = { 0 };
    uint8_t warm[16];
#endif
    uint32_t t0 = 0u, t1 = 0u;

    ssb_irq_off();
#if SS_WARMUP
    prov_encrypt(ZERO, warm);
#endif
    if (g_trig_on) ssb_trig_high();
    if (cyc) t0 = ssb_cyc_read();
    prov_encrypt(in, out);
    if (cyc) t1 = ssb_cyc_read();
    if (g_trig_on) ssb_trig_low();
    ssb_irq_on();

    if (cyc) *cyc = ssb_cyc_delta(t0, t1);
}

static void put32(uint32_t v)
{
    ssb_put((uint8_t)v);
    ssb_put((uint8_t)(v >> 8));
    ssb_put((uint8_t)(v >> 16));
    ssb_put((uint8_t)(v >> 24));
}

/* An unknown opcode is answered immediately with SS_V_RSP_ERR, so a desync
 * resolves fast and visibly rather than looking like a dead victim. */
static int payload_len(uint8_t op)
{
    switch (op) {
        case SS_V_ENC:      return SS_V_PT_LEN;
        case SS_V_ENC_CYC:  return SS_V_PT_LEN;
        case SS_V_IDENT:    return 0;
        case SS_V_SET_ACR:  return 4;
        case SS_V_TRIG_CTL: return 1;
        case SS_V_LIBINFO:  return 0;
        default:            return -1;
    }
}

static void send_ident(void)
{
    ss_libinfo_t li;
    uint8_t flags = SS_VI_HAS_TRIGGER;

    prov_libinfo(&li);
    if (li.cfg & SS_LC_RAMTAB) flags |= SS_VI_TABLES_RAM;
    if (ssb_has_cyc())         flags |= SS_VI_REPORTS_CYC;
#if SS_WARMUP
    flags |= SS_VI_WARMUP;
#endif

    ssb_put(SS_V_IDENT | SS_V_RSP);
    ssb_put((uint8_t)SS_PROTO_VERSION);
    ssb_put(flags);
    ssb_put((uint8_t)ssb_layout());
    ssb_put(0u);
    /* The first table this configuration actually HAS, so IDENT alone is
     * enough to attribute a trace count. Not simply tab[0]: wolfSSL's default
     * build has no separate byte S-box -- its Te carries it -- so slot 0 is
     * empty there and slot 1 is the address that matters. All three slots are
     * in SS_V_LIBINFO either way. */
    put32(li.tab[0] ? li.tab[0] : (li.tab[1] ? li.tab[1] : li.tab[2]));
    put32(ssb_acr_get());
    put32(ssb_clk_hz());
}

static void send_libinfo(void)
{
    ss_libinfo_t li;

    prov_libinfo(&li);
    ssb_put(SS_V_LIBINFO | SS_V_RSP);
    ssb_put(li.lib);
    ssb_put(li.cfg);
    ssb_put(li.n_tab);
    ssb_put(li.build_byte);
    put32(li.tab[0]);
    put32(li.tab[1]);
    put32(li.tab[2]);
    put32(li.code);
    put32(li.version);
}

static void execute(uint8_t op, const uint8_t *in)
{
    uint8_t out[16];

    switch (op) {
        case SS_V_ENC:
        {
            encrypt_measured(in, out, 0);
            ssb_put(SS_V_ENC | SS_V_RSP);
            for (uint32_t i = 0u; i < SS_V_CT_LEN; i++) ssb_put(out[i]);
            break;
        }

        case SS_V_ENC_CYC:
        {
            uint32_t c = 0u;

            if (!ssb_has_cyc()) {
                /* This build carries no cycle counter at all -- that is the
                 * point of the uninstrumented arm. Answering with the error
                 * response rather than with zeros means the host learns it
                 * from the device instead of reading a column of zeros as a
                 * constant-time result. */
                ssb_put(SS_V_RSP_ERR);
                ssb_put(op);
                break;
            }
            encrypt_measured(in, out, &c);
            ssb_put(SS_V_ENC_CYC | SS_V_RSP);
            for (uint32_t i = 0u; i < SS_V_CT_LEN; i++) ssb_put(out[i]);
            put32(c);
            break;
        }

        case SS_V_IDENT:
            send_ident();
            break;

        case SS_V_LIBINFO:
            send_libinfo();
            break;

        case SS_V_SET_ACR:
        {
            uint32_t v = (uint32_t)in[0] | ((uint32_t)in[1] << 8)
                       | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);

            ssb_acr_set(v);
            ssb_put(SS_V_SET_ACR | SS_V_RSP);
            /* The READBACK, never the requested value: the board layer
             * refuses a configuration this clock cannot run, and the caller
             * learns it was refused by getting the old value back. */
            put32(ssb_acr_get());
            break;
        }

        case SS_V_TRIG_CTL:
            g_trig_on = (in[0] != 0u);
            if (!g_trig_on) ssb_trig_low();
            ssb_put(SS_V_TRIG_CTL | SS_V_RSP);
            ssb_put(g_trig_on);
            break;

        default:
            ssb_put(SS_V_RSP_ERR);
            ssb_put(op);
            break;
    }
}

void ssv_run(const uint8_t key[16])
{
    uint8_t buf[SS_V_PT_LEN];
    uint8_t op = 0u;
    int need = 0, have = 0;

    ssb_init();
    prov_init(key);

    for (;;) {
        int c = ssb_get();

        if (op == 0u) {
            need = payload_len((uint8_t)c);
            if (need < 0) {
                ssb_put(SS_V_RSP_ERR);
                ssb_put((uint8_t)c);
                continue;
            }
            if (need == 0) {
                execute((uint8_t)c, buf);
                continue;
            }
            op = (uint8_t)c;
            have = 0;
            continue;
        }

        buf[have++] = (uint8_t)c;
        if (have == need) {
            uint8_t done = op;

            op = 0u;
            execute(done, buf);
        }
    }
}
