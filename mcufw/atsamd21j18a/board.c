/* Microchip ATSAMD21J18A board layer, on a SAM D21 Xplained Pro.
 *
 * UNTESTED ON SILICON. Nothing in this directory has been flashed to a part.
 * It compiles, and every register in it was copied from a rig that has run on
 * this die, but no step of the checklist in docs/PORTING.md past step 3 has
 * been done. Treat any number it produces as unverified until step 4 onward
 * have been run.
 *
 * The directory and ssb_name are both the full part number, which is the
 * repository convention and the fact a capture needs. This port spent its
 * first life in a directory called samd21, so a capture or a note naming
 * that is this part.
 *
 * WHAT IS DIFFERENT ABOUT THIS PART, relative to the ST ports beside it.
 *
 *   The accelerator is NVMCTRL.CTRLB, not a FLASH_ACR, and it has a field
 *   the ST parts do not: READMODE, a documented constant-time read mode. The
 *   packing of the three fields into the contract's one opaque word is at the
 *   top of regs.h.
 *
 *   There is NO DWT CYCLE COUNTER. Cortex-M0+ does not implement one. SysTick
 *   is the only time base and it counts DOWN over 24 bits, which is why
 *   ssb_cyc_delta owns the subtraction.
 *
 *   Flash is based at 0x00000000, so the vector table is at address zero.
 *
 *   The console is a SERCOM in USART mode, which needs its generic clock
 *   routed and its pads muxed before it exists at all. The configuration
 *   below is the one in rigs/fw_samd21_sbsfu/SAMD21_XPRO/HAL/samd21_hal.c,
 *   which has run on this board.
 *
 * The clock sequence is also that file's, including the park step, which is
 * the one piece of it that is not obvious and cost three wedged boards in the
 * research tree: clearing DFLLCTRL disables the DFLL, and doing that while
 * generator 0 is already sourced from it stops the CPU clock dead, after
 * which the part answers SWD with WAIT ACK forever and reads exactly like
 * dead hardware.
 */
#include <stdint.h>
#include "regs.h"
#include "ss_board.h"
#include "ss_victim.h"

/* 48 MHz is the default because it is what the research tree measured this
 * part's channel at and what a product ships: DFLL48M open loop, CPUSEL = /1,
 * RWS = 1, READMODE = NO_MISS_PENALTY, cache on. 8 MHz is the other build
 * point, and it exists for one reason: below 24 MHz, RWS = 0 is legal, and a
 * zero-wait-state arm is a configuration this project needs to be able to
 * reach rather than argue about. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 48
#endif
#if SS_CLK_MHZ != 48 && SS_CLK_MHZ != 8
#error "SS_CLK_MHZ must be 48 (DFLL48M open loop) or 8 (OSC8M, PRESC = 0)"
#endif

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

/* Asynchronous arithmetic baud, 16x oversampled:
 *
 *     BAUD = 65536 * (1 - 16 * f_baud / f_ref)
 *
 * IN 64-BIT. 65536 * 16 * baud overflows 32 bits at any baud above about
 * 4 kbit/s, and the overflow produces a plausible-looking wrong divisor
 * rather than a failure. This is a constant expression, so the arithmetic
 * happens in the compiler and nothing 64-bit reaches this Cortex-M0+. */
#define SS_BAUDREG ((uint16_t)(65536ULL \
        - ((65536ULL * 16ULL * (unsigned long long)(SS_BAUD)) / SS_CLK_HZ)))
#if (SS_BAUD) * 16u > SS_CLK_HZ
#error "SS_BAUD is above f_ref/16, which 16x oversampling cannot reach"
#endif

/* The only link this part offers here. The EDBG exposes a CDC port wired to
 * the target's PA22/PA23, which is SERCOM3 pad 0 and pad 1 on mux C (user
 * guide Table 4-10). The Xplained Pro EXT headers do carry other SERCOM pads,
 * but no rig in the research tree has configured one, so no `header` link is
 * declared rather than guessed at. */
#if !defined(SS_LINK_VCP)
#error "this part offers only LINK=vcp"
#endif
#define UART_TX_PIN 22u                   /* PA22, SERCOM3 PAD[0], mux C    */
#define UART_RX_PIN 23u                   /* PA23, SERCOM3 PAD[1], mux C    */
#define UART_TXPO   0u                    /* TxD on PAD[0]                  */
#define UART_RXPO   1u                    /* RxD on PAD[1]                  */

/* PB30, which is LED0 and also EXT3 pin 5 (user guide Table 4-6), so it is
 * both reachable by an instrument and visible. The LED is wired ACTIVE LOW,
 * so the board's LED is off while the trigger is asserted; the pin itself
 * follows the contract, high for high. It is not a SWD pin: SWCLK and SWDIO
 * are PA30 and PA31. */
#define TRIG_GROUP 1u
#define TRIG_PIN   30u

const char *ssb_name(void) { return "atsamd21j18a"; }

/* ---- the accelerator ---------------------------------------------------
 * See the top of regs.h for the packing. Everything below reads CTRLB back
 * rather than remembering what was asked for.
 */

/* Bounded, because a peripheral that never answers is trap 4 in
 * docs/PORTING.md: nothing in ssb_* has a timeout, so an unbounded poll stops
 * the victim mid-reply and the host reports a short response, which reads
 * like a baud problem. INVALL completes in a few cycles; this bound is four
 * orders of magnitude more than that. */
static void nvm_invall(void)
{
    uint32_t i;

    NVM_CTRLA = (uint16_t)((NVM_CMDEX_KEY << 8) | NVM_CMD_INVALL);
    __asm volatile("dsb" ::: "memory");
    for (i = 0u; i < 10000u && !(NVM_INTFLAG & NVM_INTFLAG_READY); i++) { }
    __asm volatile("isb" ::: "memory");
}

/* Write the three read-path fields together, preserving everything else.
 * READ-MODIFY-WRITE: MANW is a write-path control and SLEEPPRM a sleep-path
 * one, neither of them ours to clear, and CTRLB's reset value is not
 * documented in the SVD so nothing here assumes one. */
static void ctrlb_apply(uint32_t rws, uint32_t readmode, uint32_t cachedis)
{
    uint32_t v = NVM_CTRLB;

    v &= ~(CTRLB_RWS_Msk | CTRLB_READMODE_Msk | CTRLB_CACHEDIS);
    v |= (rws << CTRLB_RWS_Pos) & CTRLB_RWS_Msk;
    v |= (readmode << CTRLB_READMODE_Pos) & CTRLB_READMODE_Msk;
    if (cachedis) v |= CTRLB_CACHEDIS;
    NVM_CTRLB = v;
    __asm volatile("dsb; isb" ::: "memory");
    nvm_invall();
}

/* TRAP 1: REFUSE what the clock cannot run, rather than execute the next
 * instruction fetch under it. The code doing the fetching is itself in flash,
 * so too few wait states for the current clock corrupts the fetch and the
 * failure is a stop in the middle of this function, not a wrong answer later.
 * From the host that looks like an unrelated crash.
 *
 * The one numeric rule here is the datasheet's: RWS = 0 is allowed only up to
 * 24 MHz at 3.3 V (table 37-42, quoted in rigs/fw_samd21_aes/main.c). So the
 * 48 MHz build refuses RWS = 0 and the 8 MHz build does not.
 *
 * "Documented as required" and "actually required on this die" are different
 * claims, and the second is measurable. This port does NOT offer a force
 * override for it, because the override is only worth having once somebody is
 * standing at the bench ready to reset a wedged board, and nobody is: this
 * port has never been run.
 *
 * READMODE = 3 is reserved and is refused for the same reason, and a word
 * with bits set outside the packing is refused so that an accepted word and
 * its readback are always the same number.
 */
static int acr_legal(uint32_t v)
{
    uint32_t rws      = (v & D21_ACR_RWS_Msk) >> D21_ACR_RWS_Pos;
    uint32_t readmode = (v & D21_ACR_READMODE_Msk) >> D21_ACR_READMODE_Pos;

    if (v & ~(D21_ACR_RWS_Msk | D21_ACR_READMODE_Msk | D21_ACR_CACHEDIS))
        return 0;
    if (readmode > D21_READMODE_DETERMINISTIC)
        return 0;
#if SS_CLK_MHZ > 24
    if (rws == 0u)
        return 0;
#else
    (void)rws;
#endif
    return 1;
}

/* TRAP 2: reach every configuration from the same state rather than from its
 * predecessor. Lines filled under the previous configuration are still valid
 * tags, and leaving them is how a runtime reconfiguration comes to behave
 * differently from the same configuration written at reset.
 *
 * On this part the dance is short, because the cache has a real invalidate
 * command instead of a bit-toggling sequence: take the cache down, invalidate
 * it empty, then write what was asked for and invalidate again.
 */
void ssb_acr_set(uint32_t v)
{
    uint32_t rws, readmode, cachedis;

    if (!acr_legal(v)) return;

    rws      = (v & D21_ACR_RWS_Msk) >> D21_ACR_RWS_Pos;
    readmode = (v & D21_ACR_READMODE_Msk) >> D21_ACR_READMODE_Pos;
    cachedis = (v & D21_ACR_CACHEDIS) ? 1u : 0u;

    ctrlb_apply(rws, readmode, 1u);
    ctrlb_apply(rws, readmode, cachedis);
}

uint32_t ssb_acr_get(void)
{
    uint32_t b = NVM_CTRLB;
    uint32_t v;

    v  = ((b & CTRLB_RWS_Msk) >> CTRLB_RWS_Pos) << D21_ACR_RWS_Pos;
    v |= ((b & CTRLB_READMODE_Msk) >> CTRLB_READMODE_Pos) << D21_ACR_READMODE_Pos;
    if (b & CTRLB_CACHEDIS) v |= D21_ACR_CACHEDIS;
    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }

/* Chosen at build time, not at run time: changing it would change the SERCOM
 * divisor under a live link, and the DFLL has to be stopped to be restarted,
 * which takes the CPU clock with it. Reported as unavailable rather than
 * faked. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The NVM cache has no hit or miss counter anywhere in the register map.
 * Saying 0 makes the host report "none" instead of inventing numbers. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------ */

static void gclk_sync(void)
{
    uint32_t i;

    for (i = 0u; i < 100000u; i++) {
        if (!(GCLK_STATUS & GCLK_SYNCBUSY)) return;
    }
}

#if SS_CLK_MHZ == 48
/* Start DFLL48M in OPEN LOOP at the factory coarse calibration.
 *
 * COMPILED ONLY FOR THE 48 MHz BUILD, because the 8 MHz one leaves
 * generator 0 on OSC8M and never calls this. Left out rather than left
 * unused: -Wall -Wextra is on, and a warning that only the CLK=8 build
 * prints is a warning nobody reads.
 *
 * Open loop rather than closed because closed loop needs a reference routed
 * to GCLK_DFLL48M and a multiplier, which is bring-up for an accuracy nothing
 * here needs; the host measures the clock over a known SysTick count rather
 * than doing the divider arithmetic. The cost is a few per cent of DCO error,
 * which the UART absorbs at 115200 and which the SBSFU port on this board
 * runs its own UART at.
 *
 * THE ORDER IS FROM THE ERRATA. DFLLCTRL comes out of reset with ONDEMAND set
 * (SVD reset value 0x0080), and writing DFLLVAL while ONDEMAND is set can
 * hang on a part that has not been requested. So DFLLCTRL is cleared first,
 * DFLLRDY waited for, then DFLLVAL written, then ENABLE set.
 */
static void dfll_start(void)
{
    uint32_t i, coarse;

    SYSCTRL_DFLLCTRL = 0;
    __asm volatile("dsb" ::: "memory");
    for (i = 0u; i < 200000u; i++) {
        if (SYSCTRL_PCLKSR & PCLKSR_DFLLRDY) break;
    }

    coarse = (NVM_CAL_WORD1 >> 26) & 0x3Fu;
    if (coarse == 0x3Fu) coarse = 0x1Fu;       /* unprogrammed -> mid range */
    SYSCTRL_DFLLVAL = (coarse << 10) | 512u;   /* FINE mid-scale            */
    __asm volatile("dsb" ::: "memory");
    SYSCTRL_DFLLCTRL = DFLLCTRL_ENABLE;        /* MODE = 0: open loop       */
    __asm volatile("dsb" ::: "memory");
    for (i = 0u; i < 200000u; i++) {
        if (SYSCTRL_PCLKSR & PCLKSR_DFLLRDY) break;
    }
    __asm volatile("isb" ::: "memory");
}
#endif  /* SS_CLK_MHZ == 48 */

/* Wait states first and generously, then park, then the DFLL, then the
 * configuration this build actually wants. The part boots at 1 MHz (OSC8M
 * with PRESC = 3, SVD reset value 0x87070382) on silicon rated for 48, so
 * every step here is upward and the ordering rule is the whole of it:
 * never pass through fast-and-under-waited.
 *
 * RWS = 3 is safe at every clock this part supports.
 */
static void clock_init(void)
{
    uint32_t i;

    ctrlb_apply(3u, D21_READMODE_NO_MISS_PENALTY, 0u);

    /* Park generator 0 on OSC8M before touching the DFLL. See the file
     * header: this is the step that makes clearing DFLLCTRL survivable. */
    PM_CPUSEL = 0u;                                        /* CPUDIV = /1   */
    SYSCTRL_OSC8M = (SYSCTRL_OSC8M & ~OSC8M_PRESC_Msk) | OSC8M_ENABLE;
    __asm volatile("dsb" ::: "memory");
    for (i = 0u; i < 100000u; i++) {
        if (SYSCTRL_PCLKSR & PCLKSR_OSC8MRDY) break;
    }
    GCLK_GENDIV = 0u;                                      /* generator 0   */
    gclk_sync();
    GCLK_GENCTRL = 0u | (GCLK_SRC_OSC8M << GENCTRL_SRC_Pos) | GENCTRL_GENEN;
    gclk_sync();

#if SS_CLK_MHZ == 48
    dfll_start();
    GCLK_GENCTRL = 0u | (GCLK_SRC_DFLL48M << GENCTRL_SRC_Pos) | GENCTRL_GENEN;
    gclk_sync();
    PM_CPUSEL = 0u;                                        /* /1 -> 48 MHz  */
#endif
    __asm volatile("dsb; isb" ::: "memory");

    /* The configuration this image boots into, which is the one the research
     * tree measured the channel in at 48 MHz: one wait state, the NVMCTRL
     * reset values for READMODE and CACHEDIS. At 8 MHz no wait state is
     * required, and booting into the configuration the part needs rather
     * than one it merely tolerates is what keeps a result from being read as
     * "you measured something nobody ships". */
#if SS_CLK_MHZ == 48
    ssb_acr_set((1u << D21_ACR_RWS_Pos));
#else
    ssb_acr_set(0u);
#endif
}

/* ---- the link ---------------------------------------------------------
 * No APBB mask is set for PORT. Neither rig that has run on this board sets
 * one before driving pads or reading the button, which says PORT is clocked
 * out of reset; this port takes that from their behaviour rather than from a
 * reset-value table it has read. SERCOM3 is on APBC and does need its mask.
 */
static void uart_init(void)
{
    /* PA22 is even and PA23 is odd, so they share PMUX[11] and take its two
     * nibbles. INEN on the receive pad so its input buffer is live. */
    PORT_PINCFG(0u, UART_TX_PIN) = PINCFG_PMUXEN;
    PORT_PINCFG(0u, UART_RX_PIN) = PINCFG_PMUXEN | PINCFG_INEN;
    PORT_PMUX(0u, UART_TX_PIN / 2u) = (uint8_t)(PMUX_C | (PMUX_C << 4));

    PM_APBCMASK |= PM_APBCMASK_SERCOM3;
    GCLK_CLKCTRL = (uint16_t)(CLKCTRL_ID_SERCOM3_CORE
                              | (0u << CLKCTRL_GEN_Pos)    /* generator 0   */
                              | CLKCTRL_CLKEN);
    gclk_sync();

    SERCOM3_CTRLA = USART_CTRLA_SWRST;
    while (SERCOM3_SYNCBUSY & USART_SYNCBUSY_SWRST) {}

    SERCOM3_BAUD = SS_BAUDREG;
    SERCOM3_CTRLA = USART_CTRLA_MODE_INT_CLK
                  | USART_CTRLA_DORD
                  | ((uint32_t)UART_TXPO << USART_CTRLA_TXPO_Pos)
                  | ((uint32_t)UART_RXPO << USART_CTRLA_RXPO_Pos);
    SERCOM3_CTRLB = USART_CTRLB_TXEN | USART_CTRLB_RXEN;   /* 8N1           */
    while (SERCOM3_SYNCBUSY & USART_SYNCBUSY_CTRLB) {}

    SERCOM3_CTRLA |= USART_CTRLA_ENABLE;
    while (SERCOM3_SYNCBUSY & USART_SYNCBUSY_ENABLE) {}
}

void ssb_put(uint8_t c)
{
    while (!(SERCOM3_INTFLAG & USART_INTFLAG_DRE)) {}
    SERCOM3_DATA = c;
}

int ssb_get(void)
{
    while (!(SERCOM3_INTFLAG & USART_INTFLAG_RXC)) {}
    /* The error flags are sticky and do not gate RXC, so a glitched byte
     * would otherwise leave PERR, FERR or BUFOVF set for the rest of the
     * run. Cleared rather than reported: the protocol's own framing is what
     * decides whether a command survived. */
    SERCOM3_STATUS = (uint16_t)(USART_STATUS_PERR | USART_STATUS_FERR
                                | USART_STATUS_BUFOVF);
    return (int)(SERCOM3_DATA & 0xFFu);
}

/* ---- trigger ----------------------------------------------------------- */
void ssb_trig_high(void) { PORT_OUTSET(TRIG_GROUP) = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { PORT_OUTCLR(TRIG_GROUP) = (1u << TRIG_PIN); }

static void trig_init(void)
{
    PORT_OUTCLR(TRIG_GROUP) = (1u << TRIG_PIN);            /* start low     */
    PORT_PINCFG(TRIG_GROUP, TRIG_PIN) = 0u;                /* plain output  */
    PORT_DIRSET(TRIG_GROUP) = (1u << TRIG_PIN);
}

/* ---- the cycle counter -------------------------------------------------
 * THERE IS NO DWT ON THIS CORE. Cortex-M0+ does not implement DWT_CYCCNT, so
 * there is no register here to arm and no liveness check that could rescue
 * one. SysTick is the only time base: 24 bits, counting DOWN at the CPU
 * clock, wrapping every 0.35 s at 48 MHz, which is four orders of magnitude
 * longer than one encryption.
 *
 * TRAP 3: THE BOARD OWNS THE SUBTRACTION, because this counter runs the other
 * way and a caller assuming second - first would produce large positive
 * garbage rather than an error.
 *
 * TIMING=dwt is accepted by build.mk for every part, so it has to mean
 * something here, and what it means is "no counter". It does NOT silently
 * fall back to SysTick: a capture asking for the DWT arm and getting SysTick
 * would be labelled with a counter it did not use. ssb_has_cyc then returns
 * 0, SS_V_ENC_CYC answers with the error response, and the host learns it
 * from the device instead of reading a column of zeros as a constant-time
 * result.
 */
#ifdef SS_TIMING_SYSTICK
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & 0xFFFFFFu;       /* counts DOWN               */
}

static void cyc_init(void)
{
    uint32_t a, b;
    volatile int i;

    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0;
    SYST_CSR = 5u;                             /* ENABLE | CLKSOURCE = CPU  */
    __asm volatile("dsb; isb" ::: "memory");

    /* Report what is true, not what was asked for. A counter that does not
     * move makes every interval exactly zero, which is indistinguishable
     * from a constant-time cipher in every summary statistic. */
    a = SYST_CVR;
    for (i = 0; i < 64; i++) {}
    b = SYST_CVR;
    g_cyc_ok = (a != b);
}
#else
int      ssb_has_cyc(void)  { return 0; }
uint32_t ssb_cyc_read(void) { return 0u; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    (void)first; (void)second; return 0u;
}
static void cyc_init(void) {}
#endif

void ssb_irq_off(void) { irq_off(); }
void ssb_irq_on(void)  { irq_on(); }

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
