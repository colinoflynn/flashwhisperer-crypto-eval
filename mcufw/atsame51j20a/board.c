/* Microchip ATSAME51J20A board layer, for the SAM E51 Curiosity Nano.
 *
 * THIS PORT HAS NEVER BEEN RUN ON SILICON. It compiles, both residence
 * variants link and the table twin moves, and that is the whole of what has
 * been checked. No board was attached while it was written. Treat every
 * number that comes out of it as unverified until someone has flashed it
 * and run the checklist in docs/PORTING.md, and read the console note below
 * before blaming the UART for a short response.
 *
 * The accelerator half is in a better state than that sentence suggests.
 * Its registers are copied from rigs/fw_same51_aes/ in the
 * flash-accelerator-tests tree, which HAS been run on this die, and that
 * rig's findings are what the refusals here are built from. The console
 * half has no such backing.
 *
 * WHAT THIS PART IS. Two structures on the flash read path, in different
 * peripherals and separately disableable:
 *
 *   L2  NVMCTRL.CTRLA.CACHEDIS0 / CACHEDIS1, a depth-one 16-byte read latch
 *       per AHB bus port. AHB0 is the port the CPU's own reads traverse,
 *       which the rig established by disabling each alone.
 *   L1  CMCC, a 4 KB four-way unified cache with 16-byte lines, enabled by
 *       CTRL.CEN and DISABLED AT RESET. So reset is not what any product
 *       ships here, and the interesting cells are the ones with it on.
 *
 * Both are in the one opaque word the contract asks for. The packing is in
 * regs.h and nowhere else.
 *
 * THE CLOCK IS LEFT IN ITS RESET SHAPE, 48 MHz from the open-loop DFLL on
 * generator 0, with MCLK.CPUDIV at one. The rig's clock ladder reaches 12,
 * 24, 48 and 120 MHz and this port deliberately implements none of it:
 * every level but the reset one needs GCLK generator or DPLL programming
 * whose ordering against the wait state is the part of that rig that most
 * repays having a debugger attached, and nothing here can report a failure
 * except by going quiet. ssb_set_mhz returns 0, which the host reads as
 * "not available here" rather than as an error.
 *
 * THE CONSOLE IS THE UNVERIFIED HALF. The rig this port's registers come
 * from talks to its host over an SWD mailbox and never brings a UART up at
 * all, and the SAM D21 rig beside it declined to bring its SERCOM up for
 * the same reason, so there is no in-tree SERCOM code for any SAM part to
 * copy. SERCOM5 on PB16/PB17 with peripheral function C is what the SAM E51
 * Curiosity Nano user guide DS70005432 rev B gives for the nEDBG's virtual
 * COM port, and that is where the pads below come from. Three things in
 * uart_init are therefore GUESSES in the sense this repository cares about,
 * flagged again at the line that makes them:
 *
 *   * the arithmetic-mode BAUD expression,
 *   * TXPO = 0 meaning TxD on pad 0,
 *   * RXPO = 1 meaning RxD on pad 1.
 *
 * Each is the standard SAM reading and none of the three was read out of a
 * document available on this machine. If the first byte never arrives, the
 * baud divisor is the thing to check first; if bytes arrive garbled at a
 * plausible rate, it is the sample-rate field; if nothing arrives in one
 * direction only, it is the pad ordering.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

/* The reset shape, and the only clock this port programs. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 48u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "atsame51j20a offers LINK=vcp only - see mcufw/atsame51j20a/board.mk"
#endif

/* PA14 is LED0, the yellow user LED, and the only output this kit fits that
 * is not on an edge connector. The LED lights when the pin is driven LOW,
 * so it is lit between captures rather than during one. The instrument sees
 * the electrical level, which is what trig_high and trig_low name. */
#define TRIG_GROUP 0u
#define TRIG_PIN   14u

/* The virtual COM port, DS70005432 rev B section 4.1.1. */
#define VCP_GROUP  1u
#define VCP_TX_PIN 16u          /* SERCOM5 pad 0, out of the MCU */
#define VCP_RX_PIN 17u          /* SERCOM5 pad 1, into the MCU   */

#define DSB() __asm volatile("dsb" ::: "memory")
#define ISB() __asm volatile("isb" ::: "memory")

const char *ssb_name(void) { return "atsame51j20a"; }

/* ---- the accelerator ---------------------------------------------------
 *
 * TRAP 1, and on this part it fires twice for two unrelated reasons.
 *
 * The first is the usual one. Too few wait states for the current CPU clock
 * corrupts instruction fetch, and the code doing the reconfiguring is
 * itself in flash, so the failure is a fault inside the write rather than a
 * wrong answer later. The clamp comes from the read-cycle ladder the rig
 * records out of data sheet section 54, which reads as read cycles against
 * maximum frequency: 1 to 24 MHz, 2 to 51, 3 to 77, 4 to 101, 5 to 119, 6
 * to 120, above 2.7 V. RWS is read cycles minus one, so 48 MHz needs RWS at
 * least 1 and the rig's own table agrees. AUTOWS sidesteps the question by
 * construction and is set at reset, so a request carrying it is never
 * clamped on this ground.
 *
 * The second is specific to this die and is not in any data sheet.
 * CLEARING CTRLA.CACHEDIS0, that is re-enabling the AHB0 line cache, raises
 * an imprecise data bus error, measured on DSU.DID 0x61810604 by the rig
 * and recorded in its config_apply. Setting the bit is fine. So disabling
 * the line cache on the CPU's own port is ONE-WAY UNTIL A RESET, and a
 * request that would lower the bit is refused here rather than taken. That
 * refusal is worth more than the cell it costs: "turn the flash cache off
 * around the crypto and back on afterwards" is not implementable on this
 * part, and a sweep that discovers it by wedging learns the same thing far
 * more slowly.
 *
 * SS_V_SET_ACR replies with the readback, never the request, so either
 * refusal reaches the host as the old word coming back.
 *
 * TRAP 2 is the CMCC dance, and the shape of it is not optional. Every
 * documented sequence is disable, wait for SR.CSTS to agree, write CFG,
 * invalidate while disabled, then re-enable. A CFG write with the cache
 * running is not specified to take, and a readback that looks right is not
 * evidence the array was re-tagged.
 *
 * THE LINE LATCH HAS NO SUCH DANCE AVAILABLE. There is no invalidate
 * command in this NVMCTRL's command set, the SAM D21's INVALL has no
 * counterpart, and the only two routes the rig found to clearing the latch
 * are toggling CACHEDIS, which the paragraph above makes one-way, and
 * gating the line cache's separate AHB clock. Which of those actually
 * re-tags is UNESTABLISHED, and neither is done here, so a configuration
 * reached at run time carries whatever the latch already held. For the
 * latch that is a depth of one line, so the warm-up encryption the victim
 * already does in front of every measured call covers it; for the CMCC,
 * which is where it would matter, the dance above is real.
 */
#define ACR_RESET (E51_ACR_AUTOWS | (2u << E51_ACR_CSIZESW_Pos))

static uint32_t min_rws(void)
{
    /* Section 54's ladder, evaluated for whatever SS_CLK_MHZ is set to
     * rather than written out as one number, so changing the clock cannot
     * silently leave the clamp behind. */
    return (SS_CLK_HZ <=  24000000u) ? 0u
         : (SS_CLK_HZ <=  51000000u) ? 1u
         : (SS_CLK_HZ <=  77000000u) ? 2u
         : (SS_CLK_HZ <= 101000000u) ? 3u
         : (SS_CLK_HZ <= 119000000u) ? 4u : 5u;
}

static int acr_legal(uint32_t v)
{
    if (v & ~E51_ACR_KNOWN_Msk)
        return 0;
    if (!(v & E51_ACR_AUTOWS)
        && ((v & E51_ACR_RWS_Msk) >> E51_ACR_RWS_Pos) < min_rws())
        return 0;
    /* Only 1, 2 and 4 KB are defined for this field. */
    if (((v & E51_ACR_CSIZESW_Msk) >> E51_ACR_CSIZESW_Pos) > 2u)
        return 0;
    /* The one-way bit. Refuse a request that would lower it. */
    if ((NVM_CTRLA & CTRLA_CACHEDIS0) && !(v & E51_ACR_CACHEDIS0))
        return 0;
    return 1;
}

/* CTRLA whole, by read-modify-write, because WMODE and SUSPEN are not axes
 * and must survive. CTRLB is never touched: it is the write-only command
 * register and the CMDEX key sits in the byte a wait-state write reaches. */
static void nvm_ctrla(uint32_t rws, uint32_t autows, uint32_t cachedis)
{
    uint32_t t = NVM_CTRLA;

    t &= ~(CTRLA_RWS_Msk | CTRLA_AUTOWS | CTRLA_CACHEDIS0 | CTRLA_CACHEDIS1
           | CTRLA_AHBNS0 | CTRLA_AHBNS1 | CTRLA_PRM_Msk);
    t |= ((rws & 0xFu) << CTRLA_RWS_Pos)
       | (autows ? CTRLA_AUTOWS : 0u)
       | ((cachedis & 1u) ? CTRLA_CACHEDIS0 : 0u)
       | ((cachedis & 2u) ? CTRLA_CACHEDIS1 : 0u);
    NVM_CTRLA = (uint16_t)t;
    DSB(); ISB();
}

static void cmcc_wait(uint32_t want)
{
    /* Bounded, because a peripheral that never answers must not take the
     * whole victim down with it. If the bound is reached the configuration
     * is wrong and the readback will say so. */
    for (uint32_t i = 0u; i < 100000u; i++)
        if (((CMCC_SR & SR_CSTS) ? 1u : 0u) == want) return;
}

static void cmcc_set(uint32_t cen, uint32_t icdis, uint32_t dcdis,
                     uint32_t csizesw)
{
    CMCC_CTRL = 0u;
    DSB();
    cmcc_wait(0u);
    CMCC_CFG = ((csizesw & 7u) << CFG_CSIZESW_Pos)
             | (icdis ? CFG_ICDIS : 0u)
             | (dcdis ? CFG_DCDIS : 0u);
    DSB();
    CMCC_MAINT0 = MAINT0_INVALL;             /* while disabled */
    DSB();
    if (cen) {
        CMCC_CTRL = CTRL_CEN;
        DSB();
        cmcc_wait(1u);
    }
    ISB();
}

void ssb_acr_set(uint32_t v)
{
    uint32_t cachedis;

    if (!acr_legal(v)) return;

    cachedis = ((v & E51_ACR_CACHEDIS0) ? 1u : 0u)
             | ((v & E51_ACR_CACHEDIS1) ? 2u : 0u);

    /* Transit through a wait state that is safe at this clock whatever the
     * request is, carrying the LIVE CACHEDIS bits through untouched. The
     * rig's first version forced them to zero here and so bus-faulted on
     * every second call from a CACHEDIS0 state, whatever was being asked
     * for. Passing the live value through means only a request that
     * genuinely lowers the bit is affected, and acr_legal has already
     * refused that one. */
    nvm_ctrla(min_rws(), 1u,
              (uint32_t)((NVM_CTRLA & (CTRLA_CACHEDIS0 | CTRLA_CACHEDIS1)) >> 14));

    /* The cache down while the read path moves, then the request, then the
     * cache as asked for. */
    cmcc_set(0u, 0u, 0u, 2u);
    nvm_ctrla((v & E51_ACR_RWS_Msk) >> E51_ACR_RWS_Pos,
              (v & E51_ACR_AUTOWS) ? 1u : 0u, cachedis);
    cmcc_set((v & E51_ACR_CEN) ? 1u : 0u,
             (v & E51_ACR_ICDIS) ? 1u : 0u,
             (v & E51_ACR_DCDIS) ? 1u : 0u,
             (v & E51_ACR_CSIZESW_Msk) >> E51_ACR_CSIZESW_Pos);
}

uint32_t ssb_acr_get(void)
{
    uint32_t a = NVM_CTRLA;
    uint32_t c = CMCC_CFG;
    uint32_t v;

    v  = ((a & CTRLA_RWS_Msk) >> CTRLA_RWS_Pos) << E51_ACR_RWS_Pos;
    if (a & CTRLA_AUTOWS)    v |= E51_ACR_AUTOWS;
    if (a & CTRLA_CACHEDIS0) v |= E51_ACR_CACHEDIS0;
    if (a & CTRLA_CACHEDIS1) v |= E51_ACR_CACHEDIS1;

    /* CTRL is write-only on this peripheral, so the enable is read out of
     * the status register. That is a readback of what the cache IS, not of
     * what it was asked to be, which is the distinction the contract is
     * built around. */
    if (CMCC_SR & SR_CSTS)   v |= E51_ACR_CEN;
    if (c & CFG_ICDIS)       v |= E51_ACR_ICDIS;
    if (c & CFG_DCDIS)       v |= E51_ACR_DCDIS;
    v |= ((c & CFG_CSIZESW_Msk) >> CFG_CSIZESW_Pos) << E51_ACR_CSIZESW_Pos;
    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The part does count its own cache events, and this still reports none.
 * CMCC.MCFG.MODE selects one of CYCLE_COUNT, IHIT_COUNT and DHIT_COUNT into
 * a single 32-bit MSR, so there is a hit counter and there is NO MISS
 * COUNTER. The contract's hook hands back a hit and a miss together, and
 * the only miss figure available here is a subtraction against a separate
 * run, which is not what a caller reading this hook would think it was
 * getting. Reporting none is the truthful answer; the rig has the counter
 * if the single-sided figure is what is wanted. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- bring-up ----------------------------------------------------------- */
static void clock_init(void)
{
    /* Nothing to program. Generator 0 comes out of reset running from the
     * open-loop DFLL at 48 MHz with GENEN set, and CPUDIV resets to one.
     * The accelerator is then put into its reset shape through the SAME
     * path the host will use, so the boot state and a host-set state are
     * produced by identical code rather than by two sequences that have to
     * be kept agreeing. */
    MCLK_CPUDIV = 1u;
    ssb_acr_set(ACR_RESET);
}

/* ---- the link ----------------------------------------------------------- */
static void uart_init(void)
{
    uint32_t ctrla;

    /* A peripheral on bridge D reads all zeros and discards writes until
     * its APBDMASK bit is set, and APBDMASK resets to zero, so this write
     * has to come first or everything below is silently dropped. */
    MCLK_APBDMASK |= APBDMASK_SERCOM5;
    (void)MCLK_APBDMASK;

    /* The core clock, from generator 0. GEN is the low nibble. */
    GCLK_PCHCTRL(GCLK_ID_SERCOM5) = 0u | PCHCTRL_CHEN;
    DSB();

    /* PB16 and PB17 to peripheral function C. PMUX packs two pins per
     * byte, the even pin in the low nibble, so one byte covers both. */
    PORT_PMUX(VCP_GROUP, VCP_TX_PIN) = (uint8_t)(PMUX_C | (PMUX_C << 4));
    PORT_PINCFG(VCP_GROUP, VCP_TX_PIN) = PINCFG_PMUXEN;
    PORT_PINCFG(VCP_GROUP, VCP_RX_PIN) = PINCFG_PMUXEN | PINCFG_INEN;

    SERCOM5_CTRLA = USART_CTRLA_SWRST;
    while (SERCOM5_SYNCBUSY & USART_SYNCBUSY_SWRST) {}

    /* GUESS, flagged in the file header: the asynchronous arithmetic-mode
     * divisor, BAUD = 65536 * (1 - 16 * fbaud / fref), with SAMPR left at
     * zero for sixteen-times oversampling. The expression is the standard
     * SAM one and data sheet section 33.6.2.3.1 is where it lives, but that
     * text was not available while this was written, so it has been
     * reproduced rather than read. 64-bit intermediates because the
     * numerator overflows 32 at this clock. */
    SERCOM5_BAUD = (uint16_t)(65536u
        - (uint32_t)(((uint64_t)65536u * 16u * SS_BAUD) / SS_CLK_HZ));

    /* GUESS, flagged in the file header: TXPO = 0 for TxD on pad 0 and
     * RXPO = 1 for RxD on pad 1, which is what the kit's wiring needs.
     * DORD = 1 is LSB first, which is ordinary asynchronous serial. */
    ctrla = ((uint32_t)USART_CTRLA_MODE_INT << USART_CTRLA_MODE_Pos)
          | (0u << USART_CTRLA_TXPO_Pos)
          | (1u << USART_CTRLA_RXPO_Pos)
          | USART_CTRLA_DORD;
    SERCOM5_CTRLA = ctrla;

    /* Eight bits, one stop bit, no parity: CHSIZE and SBMODE both zero. */
    SERCOM5_CTRLB = USART_CTRLB_TXEN | USART_CTRLB_RXEN;
    while (SERCOM5_SYNCBUSY & USART_SYNCBUSY_CTRLB) {}

    SERCOM5_CTRLA = ctrla | USART_CTRLA_ENABLE;
    while (SERCOM5_SYNCBUSY & USART_SYNCBUSY_ENABLE) {}
}

void ssb_put(uint8_t c)
{
    while (!(SERCOM5_INTFLAG & USART_INTFLAG_DRE)) {}
    SERCOM5_DATA = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-armed here rather than in init, because a debug probe clears
     * DEMCR.TRCENA when it DISCONNECTS, which is after the board has
     * booted and after the liveness check in cyc_init has passed. Arming
     * once at boot therefore leaves the device reporting a counter it no
     * longer has, and every window then comes out exactly zero, which
     * reads as a constant-time result. The console read is outside every
     * measured region, so the cost is nothing. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        DSB(); ISB();
    }
#endif
    while (!(SERCOM5_INTFLAG & USART_INTFLAG_RXC)) {}
    return (int)(SERCOM5_DATA & 0xFFu);
}

/* ---- trigger ------------------------------------------------------------ */
void ssb_trig_high(void) { PORT_OUTSET(TRIG_GROUP) = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { PORT_OUTCLR(TRIG_GROUP) = (1u << TRIG_PIN); }

static void trig_init(void)
{
    PORT_OUTCLR(TRIG_GROUP) = (1u << TRIG_PIN);         /* start low */
    PORT_PINCFG(TRIG_GROUP, TRIG_PIN) = 0u;             /* plain output */
    PORT_DIRSET(TRIG_GROUP) = (1u << TRIG_PIN);
}

/* ---- the cycle counter --------------------------------------------------
 * TRAP 3: the board owns the subtraction, and the two counters here run in
 * opposite directions. The DWT counts UP over a full 32 bits; SysTick
 * counts DOWN over 24, so its delta is first minus second and has to be
 * masked. A caller assuming second minus first on SysTick gets large
 * positive garbage rather than an error.
 */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                       /* counts UP, 32 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    DEMCR |= DEMCR_TRCENA;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    DSB(); ISB();
    a = DWT_CYCCNT;
    for (volatile int i = 0; i < 64; i++) {}
    b = DWT_CYCCNT;
    /* CYCCNT can stop while CYCCNTENA still reads set, and then every
     * derived interval is exactly zero. Report whether it MOVED, not
     * whether the enable took. */
    g_cyc_ok = (b != a);
}

#elif defined(SS_TIMING_SYSTICK)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & 0xFFFFFFu;         /* counts DOWN, 24 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0u;
    SYST_CSR = 5u;                               /* ENABLE | core clock */
    a = SYST_CVR;
    for (volatile int i = 0; i < 64; i++) {}
    b = SYST_CVR;
    /* Same question as for the DWT and the same answer: report what is
     * true. A SysTick whose clock source never arrives reads a constant. */
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
    uart_init();
    trig_init();
    cyc_init();
}
