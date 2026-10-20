/* Renesas Synergy S5D3 (R7FS5D37A3A01CFP) board layer.
 *
 * THIS PORT IS UNTESTED ON SILICON. It was written against
 * rigs/fw_s5d3_aes/ in the research tree, which is the register source of
 * truth for it, and it has been compiled and nothing else. No board was
 * attached, nothing was flashed, and no measurement was taken with it. Every
 * number below that came off a die came off it through that rig, not through
 * this file. Three things in here are additionally NOT ESTABLISHED and are
 * called out where they sit: the console, the trigger pin, and what happens
 * if the oscillator bring-up fails.
 *
 * What makes it worth the trouble is that its accelerator is a single enable
 * bit serving instruction fetch and data reads alike, where the ST parts
 * give separate bits for the two sides.
 *
 * WHAT IS DIFFERENT ABOUT IT, beyond the registers:
 *
 *   The wait-state control is FLWT and not MEMWAIT. On the RA2A1 it is the
 *   other way round. MEMWAIT reads 0 on this die and the rig could find no
 *   effect from it.
 *
 *   SRAM is at 0x1FFE0000, not 0x20000000, so the RAM table twin lands at
 *   0x1FFE_xxxx. See memory.ld, which also records that the low flash is an
 *   ID and option block this image deliberately does not cover.
 *
 *   The flash cache needs an explicit INVALIDATE around any change, and the
 *   rig does that, so this file reproduces its sequence rather than inventing
 *   one. See ssb_acr_set.
 *
 *   HOCO is stopped at reset on this die (OFS1 bit 8 set), so the part comes
 *   up on MOCO and the only documented way to a known clock is the 12 MHz
 *   main oscillator through the PLL. Every other part here is measured at its
 *   reset clock; this one cannot be, because MOCO's frequency is nowhere in
 *   the rig and a clock figure nobody measured is worse than a bring-up.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

#if !defined(SS_LINK_HEADER) && !defined(SS_LINK_VCP)
#  error "s5d3 needs a LINK; see mcufw/s5d3/board.mk"
#endif

/* ICLK after a successful bring-up: the PLL at 240 MHz divided by two, which
 * is the tree the factory image was running and the one every measurement in
 * the rig was taken at. 0 means the bring-up did not get there, see
 * clock_init. */
#define SS_CLK_HZ_PLL 120000000u

/* The factory divider word, measured: ICK /2 = 120 MHz, FCK /4 = 60 MHz,
 * PCLKB /4 = 60 MHz. The dividers move together because this family requires
 * ICLK >= PCLKA >= PCLKB, so a tidier-looking tree is an illegal one. This
 * port does not offer a clock change, so the single value is written out
 * rather than computed. */
#define SCKDIVCR_120MHZ 0x21011221u

/* The FLWT floor at 120 MHz, from the rig: two wait states are mandatory
 * above 80 MHz, one above 40. Writing below the floor does not fault, it
 * wedges the core, and the rig records exactly that happening when FLWT was
 * driven to 0 while ICLK was 120 MHz. */
#define FLWT_MIN_120MHZ 2u

static uint32_t g_clk_hz;

const char *ssb_name(void) { return "s5d3"; }

static inline void sync(void) { __asm volatile("dsb 0xF; isb 0xF" ::: "memory"); }

/* ---- the accelerator --------------------------------------------------
 * The word is packed, FLWT in bits 0..2 and FCACHEEN in bit 8. regs.h has
 * the packing and the reason the other two candidate registers are not in
 * it.
 *
 * TRAP 1, refuse what the clock cannot run. ICLK here is 120 MHz, so FLWT
 * below 2 is refused rather than written. This is not a precaution carried
 * over from another part: the rig drove FLWT to 0 at 120 MHz and the core
 * stopped answering, with a reset the only way out. The caller learns it was
 * refused because SS_V_SET_ACR replies with the readback.
 *
 * On the failed-bring-up path the part is left on MOCO at a frequency this
 * port cannot name, and every FLWT value is accepted there because FLWT 7 is
 * legal at any clock and the floor only ever rises with the clock. The one
 * value that could be wrong, a low FLWT on a fast clock, cannot arise,
 * because that path never speeds the part up.
 *
 * TRAP 2, the cache dance. The flash cache has to be DISABLED before it is
 * invalidated and invalidated before it is re-enabled, or lines from the
 * previous configuration survive the switch and the state reached at run
 * time is not the state a reset-time write would leave. FCACHEIV is
 * self-clearing and the rig polls it to completion, so this does too. The
 * order is the rig's, unchanged: disable, invalidate, wait, enable.
 */
static int acr_legal(uint32_t v)
{
    if ((v & ~(S5D3_ACR_FLWT_Msk | S5D3_ACR_FCACHEEN)) != 0u)
        return 0;                            /* nothing else is in the word */
    if (g_clk_hz == SS_CLK_HZ_PLL && (v & S5D3_ACR_FLWT_Msk) < FLWT_MIN_120MHZ)
        return 0;
    return 1;
}

void ssb_acr_set(uint32_t v)
{
    uint8_t want_flwt;

    if (!acr_legal(v)) return;

    want_flwt = (uint8_t)(v & S5D3_ACR_FLWT_Msk);

    /* Wait states first, under the write-protection key, and always upwards
     * before anything else moves. The clock is not changing here, so the
     * raise-first order the rig needs for a divider change costs nothing and
     * is kept so the two files read the same. */
    PRCR = PRCR_UNLOCK;
    FLWT = FLWT_Msk;
    sync();
    FLWT = want_flwt;
    sync();
    PRCR = PRCR_LOCK;
    sync();

    FCACHEE = 0u;
    sync();
    FCACHEIV = FCACHEIV_FCACHEIV;
    while (FCACHEIV & FCACHEIV_FCACHEIV) { }
    if (v & S5D3_ACR_FCACHEEN) FCACHEE = FCACHEE_FCACHEEN;
    sync();
}

uint32_t ssb_acr_get(void)
{
    /* Reassembled from the two registers on every call. Never reconstructed
     * from what was requested, which is what makes a refusal visible. */
    uint32_t v = (uint32_t)(FLWT & FLWT_Msk);
    if (FCACHEE & FCACHEE_FCACHEEN) v |= S5D3_ACR_FCACHEEN;
    return v;
}

uint32_t ssb_clk_hz(void) { return g_clk_hz; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* No hit or miss counters anywhere in this part's flash cache that the rig
 * found, so the host reports none rather than inventing them. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock -------------------------------------------------------------
 * The 12 MHz main oscillator through the PLL at x20, divided by two for
 * ICLK, which is what the factory image was doing: SCKSCR 5, PLLCCR 0x2700,
 * MOMCR 0x20, SCKDIVCR 0x21011221, all measured by the rig.
 *
 * BOTH OSCILLATOR WAITS ARE BOUNDED, for the reason the rig bounds them: a
 * crystal that does not start otherwise hangs the firmware in a spin that is
 * indistinguishable over the wire from a dead console or a bad flash. On a
 * timeout the part stays where it is and g_clk_hz is left at 0.
 *
 * ssb_clk_hz REPORTING 0 IS THE NOT-ESTABLISHED ANSWER, and it is a real
 * possibility rather than a formality. The part is then running on MOCO,
 * whose frequency is not recorded anywhere in the rig, so there is no honest
 * number to report and a plausible one would be worse than none. A host that
 * sees 0 should treat every cycle count from the device as uncalibrated.
 */
#define OSC_TIMEOUT 2000000u

static void clock_init(void)
{
    uint32_t spun, fail = 0u;

    g_clk_hz = 0u;

    PRCR = PRCR_UNLOCK;

    FLWT = FLWT_Msk;                         /* before anything speeds up */
    sync();

    OPCCR = 0u;                              /* high-speed operating mode */
    for (spun = 0u; (OPCCR & OPCCR_TSF) && spun < OSC_TIMEOUT; spun++) { }
    if (spun >= OSC_TIMEOUT) fail = 1u;

    if (!fail) {
        MOMCR    = 0x20u;                    /* MODRV1 = 0b10, 12 MHz range */
        MOSCWTCR = 0x09u;
        MOSCCR   = 0u;                       /* MOSTP = 0, oscillator on */
        for (spun = 0u; !(OSCSF & OSCSF_MOSCSF) && spun < OSC_TIMEOUT; spun++) { }
        if (spun >= OSC_TIMEOUT) fail = 1u;
    }

    if (!fail) {
        PLLCCR = 0x2700u;                    /* PLIDIV /1, source main, x20 */
        PLLCR  = 0u;                         /* PLLSTP = 0, PLL on */
        for (spun = 0u; !(OSCSF & OSCSF_PLLSF) && spun < OSC_TIMEOUT; spun++) { }
        if (spun >= OSC_TIMEOUT) fail = 1u;
    }

    if (!fail) {
        SCKDIVCR = SCKDIVCR_120MHZ;
        sync();
        SCKSCR = SCKSCR_CKSEL_PLL;
        sync();
        g_clk_hz = SS_CLK_HZ_PLL;
    }

    PRCR = PRCR_LOCK;
    sync();

    /* The boot accelerator state goes through the same path the host will
     * use, so a state reached at boot and a state reached over the wire are
     * produced by identical code. This is the configuration the part ships
     * in at 120 MHz and the one the rig measured leaking. */
    ssb_acr_set(S5D3_ACR_FCACHEEN | FLWT_MIN_120MHZ);
}

/* ---- the link ----------------------------------------------------------
 * THE CONSOLE ON THIS PART IS NOT ESTABLISHED, and this is the largest gap
 * in the port.
 *
 * The rig defines no SCI, no port pins and no pin-function select registers,
 * because it does not have a console: it is driven over SWD through a
 * mailbox structure in SRAM, with the debugger writing a command word and
 * polling a status word. So there is no S5D3 UART address in the source of
 * truth, and writing one down from a family datasheet is the one thing this
 * port is not allowed to do. A wrong peripheral base does not fault here, it
 * hangs the first ssb_put, and from the host that reads as a baud or framing
 * problem.
 *
 * What is here instead is the rig's transport reduced to a byte pipe: two
 * rings in SRAM that a debugger moves bytes through, found by the symbol
 * ssb_mbox in the ELF. It compiles, it carries the protocol unchanged if
 * something drives it, and it invents nothing. NO HOST SIDE FOR IT EXISTS IN
 * THIS TREE: tools/capture.py opens a serial port, so a capture from this
 * part needs either that host-side work or the SCI registers read off the
 * part and a real UART put here in place of this.
 *
 * ssb_put blocks when the ring is full and ssb_get blocks when it is empty,
 * which with nothing attached means the victim stops at its first reply.
 * That is the correct behaviour for a pipe and it is also what a missing
 * host looks like, so check the symbol before blaming the firmware.
 */
#define SSB_MBOX_SZ   256u                   /* power of two, masked below */
#define SSB_MBOX_MAGIC 0x33443553u           /* 'S','5','D','3' little-endian,
                                              * the rig's MAGIC, so a host
                                              * that knows the rig finds it */

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t tx_head;               /* firmware writes */
    volatile uint32_t tx_tail;               /* host writes */
    volatile uint32_t rx_head;               /* host writes */
    volatile uint32_t rx_tail;               /* firmware writes */
    volatile uint32_t trig;                  /* see the trigger, below */
    volatile uint8_t  tx[SSB_MBOX_SZ];
    volatile uint8_t  rx[SSB_MBOX_SZ];
} ssb_mbox_t;

__attribute__((used)) volatile ssb_mbox_t ssb_mbox;

static void link_init(void)
{
    ssb_mbox.tx_head = 0u;
    ssb_mbox.tx_tail = 0u;
    ssb_mbox.rx_head = 0u;
    ssb_mbox.rx_tail = 0u;
    ssb_mbox.trig    = 0u;
    ssb_mbox.magic   = SSB_MBOX_MAGIC;       /* last, so a reader that finds
                                              * the magic finds a ring that
                                              * is already consistent */
}

void ssb_put(uint8_t c)
{
    uint32_t head = ssb_mbox.tx_head;
    uint32_t next = (head + 1u) & (SSB_MBOX_SZ - 1u);
    while (next == ssb_mbox.tx_tail) { }     /* full: wait, never drop */
    ssb_mbox.tx[head] = c;
    ssb_mbox.tx_head = next;
}

int ssb_get(void)
{
    uint32_t tail;
#if defined(SS_TIMING_DWT)
    /* Re-armed here rather than in init, because a debug probe clears
     * TRCENA when it DISCONNECTS, which is after boot. Arming once at boot
     * then leaves the liveness check passed and every later window reading
     * zero, which looks exactly like a constant-time result. This is outside
     * every measured region. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        sync();
    }
#endif
    tail = ssb_mbox.rx_tail;
    while (tail == ssb_mbox.rx_head) { }     /* empty: block, as a UART does */
    ssb_mbox.rx_tail = (tail + 1u) & (SSB_MBOX_SZ - 1u);
    return (int)ssb_mbox.rx[tail];
}

/* ---- trigger -----------------------------------------------------------
 * THERE IS NO TRIGGER PIN ESTABLISHED ON THIS PART. The rig drives no GPIO
 * at all, so neither a port register address nor a pin number can be taken
 * from it, and a pin chosen off a board schematic nobody has looked at would
 * be a guess that produces a flat trace rather than an error.
 *
 * These two are therefore a single store each to the mailbox, which is the
 * right shape (two writes, no branches, nothing else inside the window) and
 * costs an SRAM write rather than a peripheral one. An externally timed
 * measurement on this part needs a real pin here first. The on-chip counter
 * does not depend on this and is unaffected.
 */
void ssb_trig_high(void) { ssb_mbox.trig = 1u; }
void ssb_trig_low(void)  { ssb_mbox.trig = 0u; }

/* ---- the cycle counter -------------------------------------------------
 * The board owns the subtraction, because the two counters run in opposite
 * directions and over different widths. DWT CYCCNT counts UP over 32 bits
 * and SysTick counts DOWN over 24, so a caller doing second - first would
 * get large positive garbage from one of them rather than an error.
 *
 * Both are available here. The rig reads DWT_CTRL = 0x40000001 on this die,
 * with bit 25 NOCYCCNT CLEAR, so CYCCNT is implemented; that is the second
 * of the three ways this part differs from the RA2A1, which has no CYCCNT
 * and has to time with SysTick.
 *
 * ssb_has_cyc reports what is true rather than what is configured: each arm
 * reads its counter twice around a short loop and answers 0 if it did not
 * move. CYCCNT can stop while CYCCNTENA still reads set, and a debugger
 * connection hides it, so the enable bit is not evidence.
 */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                   /* counts UP, 32 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    DEMCR |= DEMCR_TRCENA;                   /* without it a firmware-side
                                              * read of DWT_CTRL returns 0 */
    DWT_CYCCNT = 0u;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    sync();
    a = DWT_CYCCNT;
    for (volatile int i = 0; i < 64; i++) { }
    b = DWT_CYCCNT;
    g_cyc_ok = (b != a);
}

#elif defined(SS_TIMING_SYSTICK)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & 0xFFFFFFu;     /* counts DOWN, 24 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0u;
    SYST_CSR = 5u;                           /* ENABLE, core clock */
    a = SYST_CVR;
    for (volatile int i = 0; i < 64; i++) { }
    b = SYST_CVR;
    g_cyc_ok = (b != a);
}

#else
int      ssb_has_cyc(void)  { return 0; }
uint32_t ssb_cyc_read(void) { return 0u; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    (void)first; (void)second; return 0u;
}
static void cyc_init(void) { }
#endif

void ssb_irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
void ssb_irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

char ssb_layout(void)
{
#if defined(SS_LAYOUT)
    return (char)SS_LAYOUT;
#else
    return '?';
#endif
}

void ssb_init(void)
{
    clock_init();
    link_init();
    cyc_init();
}
