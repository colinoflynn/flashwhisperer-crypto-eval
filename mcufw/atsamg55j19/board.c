/* Microchip ATSAMG55J19 (SAM G55 Xplained Pro) board layer.
 *
 * THIS PORT HAS NEVER BEEN RUN ON SILICON. It compiles and nothing more. No
 * board was attached when it was written, nothing here has been flashed,
 * and no byte of it has answered a protocol command. Every number in it is
 * transcribed from a source named at the point of use, and the two sources
 * are not equally strong: the flash controller, the cache, the clock status
 * registers and the watchdog come from rigs/fw_samg55_aes/ in the research
 * tree, which has been on this part and read these registers back, while
 * the console and the trigger pin come from vendor headers and have been
 * exercised by nothing. Treat the first measurement off this port as
 * bring-up, not as data, and work the checklist in docs/PORTING.md in
 * order - the UART is the half most likely to be wrong, and a UART that is
 * wrong looks like a part that does not answer rather than like a mistake.
 *
 * WHAT IS DIFFERENT ABOUT THIS PART, in the order it matters.
 *
 * The read path is TWO structures in two peripherals. The EEFC at
 * 0x400E0A00 has a 128-bit read latch that no register in the map reaches,
 * and the CMCC at 0x4003C000 is a 4-way cache sitting behind it. They were
 * measured separately by the research tree and they behave differently, so
 * the accelerator word carries both and regs.h documents the packing.
 *
 * The flash controller's own knobs are mostly INSTRUCTION-path knobs, which
 * is the opposite of the usual shape here. EEFC_FMR carries FWS, SCOD
 * (sequential code optimisation disable), CLOE (code loop optimisation
 * enable) and FAM (flash access mode). Two of those four are about code.
 * FAM is the interesting one: it is a software-settable READ WIDTH, the
 * only one in this corpus, and setting it moves the attack's row map. That
 * is recorded in eval/mcu/atsamg55j19/eval.mk, because a line shift that
 * changes with a configuration bit is a measurement property.
 *
 * The wait state is a PROGRAMMED INSERTION COUNT rather than a clock-
 * derived minimum. FWS is four bits and may legally be set anywhere above
 * what the clock needs, so it works as a gain factor at a fixed clock. Too
 * LOW is the direction that corrupts instruction fetch, which is what
 * ssb_acr_set refuses.
 *
 * The clock is the reset fast RC and this port does not move it. The rig
 * can select 8, 16 or 24 MHz through CKGR_MOR.MOSCRCF and does so with the
 * 0x37 key, but a clock axis is a measurement choice rather than a board
 * requirement, and every transition needs the safe-wait-state dance the rig
 * carries. Leaving it out keeps this file to what can be checked without a
 * board. ssb_set_mhz therefore returns 0, which the host reads as "not
 * available here" rather than as an error.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

/* The reset clock, as a nominal figure. The part boots on the fast RC with
 * MOSCRCF selecting 8 MHz and MCKR.PRES = 0; the vendor's own
 * system_samg55.c says 8 MHz is the reset selection. The research tree
 * MEASURED the resulting MCK at 7.913 MHz on this board, about 1.1 % low,
 * which is ordinary for an untrimmed RC and matters here only for the baud
 * divider - see uart_init. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 8u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "atsamg55j19 offers LINK=vcp only - see mcufw/atsamg55j19/board.mk"
#endif

/* The vendor-faithful accelerator word, and what the board boots into:
 * FWS = 0, CLOE on, SCOD and FAM off, the CMCC disabled, and PRGCSIZE left
 * at its reset encoding of 2. The pack's system_samg55.c sets CLOE at every
 * frequency, leaves SCOD and FAM alone and never enables the cache, so this
 * is what a product built on the vendor's own startup runs. The research
 * tree measured that word as the leakiest of the eight SCOD/FAM/CLOE
 * corners on this part, which is a reason to boot into it rather than a
 * reason to avoid it: the shipped configuration is the one worth measuring
 * first. */
#define SSB_ACR_RESET ((2u << SSB_ACR_PRGCSIZE_Pos) | SSB_ACR_CLOE)

/* PA6 is LED0 on the SAM G55 Xplained Pro.
 *
 * AND THE VENDOR BOARD HEADER CONTRADICTS ITSELF ABOUT IT, so this is a
 * guess rather than a fact. samg55_xplained_pro.h gives LED0 twice as PA6
 * (LED0_GPIO and LED0_PIN) and once as PA16 (PIN_LED_0), and separately
 * claims PA6 as EDBG_UART_TX_PIN in a block that also names a UART0 this
 * part does not have - boilerplate carried over from another Xplained Pro
 * board. PA6 is taken here because two of the three definitions agree on
 * it, not because it has been checked. CHECK IT AGAINST THE BOARD before
 * trusting an externally triggered capture; nothing else in this file
 * depends on it, so a wrong pin costs a trigger and no cipher result. */
#define TRIG_PIN_MASK PIO_PA6

const char *ssb_name(void) { return "atsamg55j19"; }

/* ---- the accelerator ---------------------------------------------------
 *
 * TRAP 1: refuse what the clock cannot run. The vendor's wait-state table
 * for this part is 14 + 14n MHz, so FWS = n covers up to 14 (n + 1) MHz and
 * the minimum legal value at a given clock follows from that. At the 8 MHz
 * boot clock every value from 0 up is legal and this never fires - the same
 * situation as the F401 - but the refusal is the thing that would catch a
 * clock axis added later, and under-programming the wait state does not
 * fault: it changes how the very next instruction fetch is served, and the
 * code doing the fetching is itself in flash.
 *
 * The second refusal is for a bit outside the packing. A host that sends an
 * STM32 FLASH_ACR word to this part would otherwise set FAM, which silently
 * halves the flash read width and silently invalidates the row map every
 * trace is scored against.
 *
 * TRAP 2: the cache dance. CMCC_CFG is only writable while the cache is
 * disabled, and an invalidate is specified against a disabled cache too, so
 * reaching a configuration at run time means disable, wait for the status
 * bit to clear, write CFG, invalidate, then enable or do not. Writing the
 * enable bit alone would leave whatever lines were already resident, and
 * the difference between that and a reset-time write shows up as a
 * measurement rather than as an error. CMCC_CTRL is write-only, so nothing
 * here assumes the write took: the readback is CMCC_SR.CSTS.
 *
 * EEFC_FMR takes no key and needs no unlock. It is written as one whole
 * word rather than read-modify-written, because it has no other live fields
 * and a read-modify-write would carry the previous configuration into the
 * next one. regs.h says where that is established from.
 */
static uint32_t min_fws(void)
{
    uint32_t n = (SS_CLK_HZ + 13999999u) / 14000000u;   /* ceil(f / 14 MHz) */
    return (n == 0u) ? 0u : (n - 1u);
}

static int acr_legal(uint32_t v)
{
    if (v & ~SSB_ACR_VALID_Msk)              return 0;
    if ((v & SSB_ACR_FWS_Msk) < min_fws())   return 0;
    return 1;
}

static void cmcc_disable(void)
{
    CMCC_CTRL = 0u;
    __asm volatile("dsb" ::: "memory");
    /* Bounded, because nothing in ssb_* has a timeout and a peripheral that
     * never answers stops the victim mid-reply, which the host reads as a
     * short response and blames on the UART. */
    for (uint32_t i = 0; i < 10000u && (CMCC_SR & SR_CSTS); i++) { }
}

void ssb_acr_set(uint32_t v)
{
    uint32_t fmr, cfg;

    if (!acr_legal(v)) return;

    fmr = ((v & SSB_ACR_FWS_Msk) << FMR_FWS_Pos)
        | ((v & SSB_ACR_SCOD) ? FMR_SCOD : 0u)
        | ((v & SSB_ACR_FAM)  ? FMR_FAM  : 0u)
        | ((v & SSB_ACR_CLOE) ? FMR_CLOE : 0u);

    EEFC_FMR = fmr;
    __asm volatile("dsb; isb");

    cmcc_disable();

    cfg = CMCC_CFG & ~(CFG_ICDIS | CFG_DCDIS | CFG_PRGCSIZE_Msk);
    if (v & SSB_ACR_ICDIS) cfg |= CFG_ICDIS;
    if (v & SSB_ACR_DCDIS) cfg |= CFG_DCDIS;
    cfg |= ((v & SSB_ACR_PRGCSIZE_Msk) >> SSB_ACR_PRGCSIZE_Pos)
           << CFG_PRGCSIZE_Pos;
    CMCC_CFG = cfg;
    __asm volatile("dsb" ::: "memory");

    CMCC_MAINT0 = MAINT0_INVALL;
    __asm volatile("dsb" ::: "memory");

    if (v & SSB_ACR_CEN) {
        CMCC_CTRL = CTRL_CEN;
        __asm volatile("dsb; isb");
    }
}

/* Read, never reconstructed from what was asked for. That is what makes a
 * refused configuration visible instead of being recorded under the name it
 * was requested as. */
uint32_t ssb_acr_get(void)
{
    uint32_t fmr = EEFC_FMR;
    uint32_t cfg = CMCC_CFG;
    uint32_t v;

    v  = (fmr & FMR_FWS_Msk) >> FMR_FWS_Pos;
    if (fmr & FMR_SCOD) v |= SSB_ACR_SCOD;
    if (fmr & FMR_FAM)  v |= SSB_ACR_FAM;
    if (fmr & FMR_CLOE) v |= SSB_ACR_CLOE;
    if (CMCC_SR & SR_CSTS) v |= SSB_ACR_CEN;
    if (cfg & CFG_ICDIS) v |= SSB_ACR_ICDIS;
    if (cfg & CFG_DCDIS) v |= SSB_ACR_DCDIS;
    v |= ((cfg & CFG_PRGCSIZE_Msk) >> CFG_PRGCSIZE_Pos)
         << SSB_ACR_PRGCSIZE_Pos;
    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }

int ssb_set_mhz(uint32_t mhz)
{
    /* Not offered. The hardware can do 8 / 16 / 24 MHz from the fast RC and
     * the rig does it, but every transition has to pass through a safe wait
     * state first and none of that can be checked without a board. */
    (void)mhz;
    return 0;
}

/* ---- the cache hit counter ---------------------------------------------
 *
 * This part has one, the research tree used it, and it is ASYMMETRIC in a
 * way the contract's two-value shape cannot express. CMCC_MSR is a single
 * running event count and CMCC_MCFG.MODE selects which event it counts: 0
 * cycles, 1 instruction hits, 2 data hits. The board selects data hits,
 * which is the one a table read shows up in.
 *
 * THERE IS NO MISS COUNTER ANYWHERE IN THIS BLOCK. `*miss` is therefore set
 * to zero and that zero is not a measurement - it means the part does not
 * have the number, the same way ssb_has_monitors() == 0 would. Anything
 * reading it as a miss count is reading a value nothing produced.
 */
int ssb_has_monitors(void) { return 1; }

void ssb_monitors_reset(void)
{
    CMCC_MEN  = 0u;
    CMCC_MCFG = 2u & MCFG_MODE_Msk;          /* data hits */
    CMCC_MCTRL = MCTRL_SWRST;
    __asm volatile("dsb" ::: "memory");
    CMCC_MEN  = MEN_MENABLE;
    __asm volatile("dsb" ::: "memory");
}

void ssb_monitors_read(uint32_t *hit, uint32_t *miss)
{
    *hit  = CMCC_MSR;
    *miss = 0u;                              /* no such counter - see above */
}

/* ---- clock -------------------------------------------------------------- */
/* Drive the fast RC to `mhz` (8, 16 or 24), one MOSCRCF step at a time.
 *
 * Writes to CKGR_MOR without the key in bits 16..23 are discarded in silence,
 * so the key goes on every write here. The settle wait is on PMC_SR.MOSCRCS
 * rather than a delay loop, because a delay loop calibrated at one frequency
 * is wrong at the others by construction. */
static void rc_set(uint32_t mhz)
{
    uint32_t want = (mhz >= 24u) ? 2u : (mhz >= 16u) ? 1u : 0u;
    uint32_t now  = (CKGR_MOR & CKGR_MOR_MOSCRCF_Msk) >> CKGR_MOR_MOSCRCF_Pos;

    while (now != want) {
        now = (now < want) ? (now + 1u) : (now - 1u);
        CKGR_MOR = CKGR_MOR_KEY | CKGR_MOR_MOSCRCEN
                 | (now << CKGR_MOR_MOSCRCF_Pos);
        while ((PMC_SR & PMC_SR_MOSCRCS) == 0u) { }
    }
}

static void clock_init(void)
{
    /* UNLOCK THE PMC FIRST, before anything tries to enable a peripheral
     * clock. This board comes up with PMC_WPMR.WPEN set -- measured, not
     * assumed -- and while it is set a write to PMC_PCER0 is discarded
     * without any error. The symptom is not a dead part but a half-live one:
     * FLEXCOM_MR and US_MR keep their written values because they sit in a
     * domain that does not need the peripheral clock, while US_BRGR stays 0
     * and the USART never transmits or receives. PCSR0 is the register that
     * tells the truth, so check it rather than PCER0, which is write-only. */
    PMC_WPMR = PMC_WPMR_UNLOCK;

    /* SET the fast RC to the frequency this build was compiled for, rather
     * than trusting the reset selection. MEASURED on silicon: this board came
     * up with MOSCRCF = 2, so MCK was 24 MHz while SS_CLK_MHZ said 8. Two
     * things break on that and neither announces itself:
     *
     *   - the baud divider is derived from SS_CLK_HZ, so CD=4/FP=3 produced
     *     342 857 baud instead of 114 286 and the host read garbage;
     *   - min_fws() is derived from SS_CLK_HZ too, so at 24 MHz the legal
     *     floor is FWS 1 and acr_legal() would REFUSE the FWS 0 word that
     *     this part's open cell is entirely about. ssb_acr_set() refuses by
     *     returning without writing, so the capture would have measured the
     *     previous word under the new word's label.
     *
     * MOSCRCF is stepped one notch at a time, as the datasheet sequences it,
     * waiting for PMC_SR.MOSCRCS to say the RC has settled at each step. */
    rc_set(SS_CLK_MHZ);

    /* The accelerator is set through the same path the host will use, so the
     * boot state and a host-set state are produced by identical code. */
    ssb_acr_set(SSB_ACR_RESET);
}

/* ---- the link -----------------------------------------------------------
 *
 * USART7 inside FLEXCOM7, on PA27 (RXD7) and PA28 (TXD7), peripheral B.
 * There is no standalone UART peripheral on this part at all: every serial
 * port is a FLEXCOM wrapper that selects USART, SPI or TWI, and the USART
 * registers do not answer until FLEXCOM_MR.OPMODE says USART. This is the
 * port samg55_xplained_pro.h calls CONSOLE_UART, which is the EDBG virtual
 * COM port, and hardware/boards.py in the research tree records the host
 * side of it as COM54 while noting that nothing there uses it.
 *
 * THE BAUD DIVIDER NEEDS THE FRACTIONAL FIELD AT THIS CLOCK, and that is
 * not a refinement. MCK / (16 x 115200) is 4.34 at 8 MHz, so an integer
 * divider gives 4, which is 125 000 baud and 8.5 % fast - far outside what
 * a receiver tolerates, and it would read as a dead or garbling device.
 * US_BRGR has a three-bit fractional part in eighths, and CD = 4 with
 * FP = 3 gives 4.375, which is 114 286 baud and 0.8 % slow. Against the
 * 7.913 MHz the clock MEASURES rather than the 8 MHz it is nominally, the
 * same divider gives 113 043, about 1.9 % slow, which is still inside the
 * usual few-percent budget. If the link turns out unreliable on silicon,
 * this calculation and the measured MCK are the first thing to re-derive,
 * not the last.
 */
static void uart_init(void)
{
    uint32_t eighths, cd, fp;

    PMC_PCER0 = (1u << ID_PIOA);
    PMC_PCER0 = (1u << ID_FLEXCOM7);

    /* Hand both pins to peripheral B. ABCDSR0 selects A/B within a pair and
     * ABCDSR1 selects the pair, so B is bit set in the first and clear in
     * the second. */
    PIOA_ABCDSR0 |=  (PIO_PA27_RXD7 | PIO_PA28_TXD7);
    PIOA_ABCDSR1 &= ~(PIO_PA27_RXD7 | PIO_PA28_TXD7);
    PIOA_PDR      =  (PIO_PA27_RXD7 | PIO_PA28_TXD7);
    PIOA_PUER     =   PIO_PA27_RXD7;         /* idle-high RX with no cable */

    FLEXCOM7_MR = FLEXCOM_MR_OPMODE_USART;

    US_CR  = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;
    US_IDR = 0xFFFFFFFFu;                    /* nothing may interrupt */
    US_MR  = US_MR_MODE_NORMAL | US_MR_USCLKS_MCK | US_MR_CHRL_8_BIT
           | US_MR_PAR_NO | US_MR_NBSTOP_1 | US_MR_CHMODE_NORM;

    /* The divider in eighths, rounded, then split into CD and FP. */
    eighths = (SS_CLK_HZ * 8u + 8u * SS_BAUD) / (16u * SS_BAUD);
    if (eighths < 8u) eighths = 8u;          /* CD = 0 disables the clock */
    cd = eighths / 8u;
    fp = eighths % 8u;
    US_BRGR = (cd << US_BRGR_CD_Pos) | (fp << US_BRGR_FP_Pos);

    US_CR = US_CR_RXEN | US_CR_TXEN;
}

void ssb_put(uint8_t c)
{
    while (!(US_CSR & US_CSR_TXRDY)) {}
    US_THR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-arm TRCENA here, not in init: a debug probe clears it when it
     * DISCONNECTS, after the board has booted, so arming once at boot arms
     * it and then has it switched off, with the liveness check already
     * passed, leaving the device reporting a counter it does not have. The
     * console read is outside every measured region. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    while (!(US_CSR & US_CSR_RXRDY)) {}
    return (int)(US_RHR & 0xFFu);
}

/* ---- trigger ------------------------------------------------------------ */
void ssb_trig_high(void) { PIOA_SODR = TRIG_PIN_MASK; }
void ssb_trig_low(void)  { PIOA_CODR = TRIG_PIN_MASK; }

static void trig_init(void)
{
    PMC_PCER0 = (1u << ID_PIOA);
    PIOA_CODR = TRIG_PIN_MASK;               /* start low */
    PIOA_PER  = TRIG_PIN_MASK;               /* PIO, not a peripheral */
    PIOA_OER  = TRIG_PIN_MASK;
    PIOA_PUDR = TRIG_PIN_MASK;
}

/* ---- the cycle counter --------------------------------------------------
 *
 * TRAP 3: the board owns the subtraction, and the two counters here run in
 * opposite directions. DWT CYCCNT counts UP over 32 bits; SysTick counts
 * DOWN over 24. A caller assuming second - first would get large positive
 * garbage from the SysTick arm rather than an error.
 *
 * Both arms check that the counter actually moves before claiming to have
 * one. On the DWT side that is not theoretical: ARMv7E-M implements CYCCNT
 * optionally, the counter can stop while CYCCNTENA still reads set, and a
 * probe clearing DEMCR.TRCENA on disconnect leaves every interval exactly
 * zero, which reads as a constant-time result rather than as a fault. The
 * research tree's rig ran this same probe on this part and found the
 * counter live, with DWT_CTRL reading back 0x40000001, so the DWT arm is
 * expected to work here - but ssb_has_cyc reports the probe, not the
 * expectation.
 */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                   /* counts UP over 32 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0u;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    __asm volatile("dsb; isb");
    a = DWT_CYCCNT;
    for (volatile int i = 0; i < 64; i++) {}
    b = DWT_CYCCNT;
    g_cyc_ok = (b != a);
}

#elif defined(SS_TIMING_SYSTICK)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & SYST_MASK;     /* counts DOWN over 24 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    SYST_RVR = SYST_MASK;
    SYST_CVR = 0u;
    SYST_CSR = 5u;                           /* ENABLE | core clock */
    __asm volatile("dsb; isb");
    a = SYST_CVR;
    for (volatile int i = 0; i < 64; i++) {}
    b = SYST_CVR;
    /* Same reason as the DWT arm: report what is true. A SysTick left
     * clocked from a source this part does not drive would sit still, and a
     * column of zero deltas is indistinguishable from constant time. */
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
