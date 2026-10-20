/* nRF52840-DK board layer.
 *
 * THIS PORT IS UNTESTED ON SILICON. It was written against the register
 * definitions in the research tree's nRF rigs (rigs/fw_nrf_aes,
 * rigs/fw_nrf_probe, rigs/fw_map_nrf) and Nordic's MDK headers, and it has
 * been compiled and nothing more. No board was attached, nothing was
 * flashed, and no byte has been off a wire. Steps 4 through 7 of
 * docs/PORTING.md's checklist - IDENT agreeing with `make addr`, ciphertexts
 * matching the reference AES, a bit-exact RAM twin, and a recovery - have
 * not been run. Treat every number this file could produce as unconfirmed
 * until they have.
 *
 * WHY THIS PART IS WORTH HAVING. It is the control the ST parts cannot be. It has no data cache at all and no
 * wait-state register anywhere, so the two knobs every ST board file sweeps
 * do not exist on it - and the research tree has already shown it gives up
 * a full AES-128 key regardless, through a 16-byte flash read latch that no
 * register reaches. That is the measurement that stops "the channel is an
 * ST property" and "a big enough cache makes it safe" from being arguable.
 * See research/targets/nrf52840.md in the research tree.
 *
 * Clock is 64 MHz, which is the only core rate this part has.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

/* 64 MHz, and not a build-time choice: the nRF52840's core runs at 64 MHz
 * from either HFCLK source. Nordic's HFCLKSTAT.SRC enumeration names the
 * internal RC and the crystal both as 64 MHz, so selecting the crystal buys
 * accuracy rather than speed. There is no PLL to program and no divider in
 * the path that this file touches. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 64u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

/* Nordic's BAUDRATE is a published constant per rate, not a divisor to
 * compute, so an unlisted baud has no honest value to program. Fail the
 * build rather than pick the nearest: a UART running at a rate nobody
 * selected is a framing error at the host, which reads as a dead victim. */
#if   SS_BAUD == 9600
#define SS_BAUD_REG UART_BAUDRATE_9600
#elif SS_BAUD == 57600
#define SS_BAUD_REG UART_BAUDRATE_57600
#elif SS_BAUD == 115200
#define SS_BAUD_REG UART_BAUDRATE_115200
#elif SS_BAUD == 230400
#define SS_BAUD_REG UART_BAUDRATE_230400
#elif SS_BAUD == 460800
#define SS_BAUD_REG UART_BAUDRATE_460800
#elif SS_BAUD == 921600
#define SS_BAUD_REG UART_BAUDRATE_921600
#elif SS_BAUD == 1000000
#define SS_BAUD_REG UART_BAUDRATE_1M
#else
#error "no Nordic BAUDRATE constant for this SS_BAUD - add it from nrf52840_bitfields.h"
#endif

#if !defined(SS_LINK_VCP)
#  error "nrf52840 offers LINK=vcp only - see mcufw/nrf52840/board.mk"
#endif

/* The console pins. P0.06 is TXD and P0.08 is RXD, which is how the
 * nRF52840-DK wires its on-board J-Link OB virtual COM port, and it is what
 * all three nRF rigs in the research tree use. */
#define UART_TX_PIN 6u
#define UART_RX_PIN 8u

/* The trigger pin, and THE ONE THING IN THIS FILE TAKEN FROM A BOARD
 * DOCUMENT RATHER THAN FROM A RIG. None of the nRF rigs drives a trigger,
 * so the project has not established a pin for this part. P0.13 is LED1 on
 * the nRF52840-DK, which Nordic's own board header for that board
 * (pca10056.h, LED_1 = NRF_GPIO_PIN_MAP(0,13)) is the source for. It is a
 * plain GPIO with nothing else on it and it is brought out, so it is a
 * reasonable choice - but if the probe ends up on a different board, or on
 * a different pin of this one, this is the line to change and the only one.
 * The register writes around it are not a guess; OUTSET, OUTCLR and DIRSET
 * are all in Nordic's NRF_GPIO_Type. */
#define TRIG_PIN 13u

const char *ssb_name(void) { return "nrf52840"; }

/* ---- the accelerator word ---------------------------------------------
 * The packing, and the reason the word is almost empty, are written out at
 * the top of regs.h. The short version for anyone reading only this file:
 * the word is NVMC.ICACHECNF verbatim, the cache it controls is on the
 * ICODE bus and cannot reach the data path being measured, and a null on
 * this axis is the expected result rather than a fault.
 *
 * TRAP 1, refuse what cannot be run. On an ST part this is about the clock:
 * under-programming flash latency does not fault, it changes how the next
 * instruction fetch is served. THAT FAILURE MODE DOES NOT EXIST HERE,
 * because there is no latency field and no clock-dependent legality at all,
 * and saying so is better than implying this part was checked against a
 * table it does not have. What is still refusable is a word carrying bits
 * the register does not implement, and those are refused - so a host that
 * sends an ST-shaped word like 0x700 by mistake gets the old value back
 * instead of a silent partial write recorded under the wrong name.
 *
 * TRAP 2, the reset-equivalent dance. Nordic's header documents that
 * writing CACHEEN = 0 invalidates every cache entry
 * (NVMC_ICACHECNF_CACHEEN_Disabled, "Disable cache. Invalidates all cache
 * entries."). So the dance is: drop to zero first, which both disables and
 * invalidates, then write the wanted value into a cache that is empty. Just
 * writing the enable bit would leave whatever lines a previous
 * configuration had already filled, and a state reached at run time would
 * not be the state a reset-time write produces.
 */
void ssb_acr_set(uint32_t v)
{
    if (v & ~(uint32_t)NRF_ACR_MASK) return;

    NVMC_ICACHECNF = 0u;
    __asm volatile("dsb; isb");
    NVMC_ICACHECNF = v;
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void) { return NVMC_ICACHECNF; }
uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* ---- the hit/miss counters ---------------------------------------------
 * This part does have them, which most of the parts here do not: NVMC.IHIT
 * and NVMC.IMISS. They are instruction-side, counting fetches through the
 * ICODE cache, and the research tree used exactly these two registers to
 * prove the cache never sees a data access - a flash data walk moves
 * neither counter while a code walk over the same flash moves both.
 *
 * THEY ONLY COUNT WHILE CACHEPROFEN IS SET, so ssb_has_monitors is gated on
 * that bit rather than hardcoded to 1. The registers are present either
 * way, but with profiling off they read zero, and a host told "yes, this
 * part has monitors" would read those zeros as "no misses" - a wrong number
 * rather than an absence, which is the failure this whole contract is
 * shaped around. Set bit 8 of the accelerator word (0x101) and they answer.
 */
int ssb_has_monitors(void)
{
    return (NVMC_ICACHECNF & NVMC_ICACHECNF_CACHEPROFEN) ? 1 : 0;
}

void ssb_monitors_reset(void)
{
    NVMC_IHIT = 0u;
    NVMC_IMISS = 0u;
    __asm volatile("dsb; isb");
}

void ssb_monitors_read(uint32_t *hit, uint32_t *miss)
{
    *hit  = NVMC_IHIT;
    *miss = NVMC_IMISS;
}

/* ---- clock ------------------------------------------------------------- */
static void clock_init(void)
{
    /* Start the crystal and wait for it. The core is already running at
     * 64 MHz off the internal RC before this, so what the wait buys is the
     * frequency accuracy the UART needs, not the rate.
     *
     * THIS SPIN IS UNBOUNDED, exactly as the research tree's nRF rigs write
     * it, and on a board with no HFXO fitted it would never return. That is
     * deliberate: a bounded wait that fell through would leave the console
     * running off an RC oscillator whose error the host would see as
     * occasional framing damage rather than as a missing crystal. The
     * nRF52840-DK has the crystal. A bare module might not. */
    CLOCK_HFCLKSTARTED = 0u;
    CLOCK_HFCLKSTART = 1u;
    while (!CLOCK_HFCLKSTARTED) {}

    /* Leave the accelerator where a reset leaves it, through the same path
     * the host will use, so the boot state and a host-set state are
     * produced by identical code. Reset on this part is cache OFF, which is
     * also what Nordic's own startup leaves unless something enables it. */
    ssb_acr_set(0u);
}

/* ---- the link ----------------------------------------------------------
 * Nordic's UART is driven by tasks and events, not by status flags: write 1
 * to a task to start something, poll an event for 1 and clear it by writing
 * 0. There is no clock-enable register to set first, which is why this has
 * none of the RCC lines an ST board file opens with. */
static void uart_init(void)
{
    /* Drive TXD high before the peripheral takes the pin, so the line idles
     * in the marking state instead of glitching a start bit at the host. */
    P0_OUTSET = (1u << UART_TX_PIN);
    P0_DIRSET = (1u << UART_TX_PIN);

    UART_PSELTXD = UART_TX_PIN;
    UART_PSELRXD = UART_RX_PIN;
    UART_BAUDRATE = SS_BAUD_REG;
    UART_ENABLE = UART_ENABLE_ENABLED;
    UART_STARTTX = 1u;
    UART_STARTRX = 1u;
}

void ssb_put(uint8_t c)
{
    /* Clear the event BEFORE the write as well as after. An event register
     * stays set until something writes 0 to it, so a wait entered without
     * clearing first would pass on the previous byte's event and the next
     * write would overrun the transfer still in flight. The trailing clear
     * is then redundant, and is kept because it is how the research tree's
     * nRF rigs write it and a diff against them should read. */
    UART_EV_TXDRDY = 0u;
    UART_TXD = (uint32_t)c;
    while (!UART_EV_TXDRDY) {}
    UART_EV_TXDRDY = 0u;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-arm TRCENA here, not in init: a debug probe clears it when it
     * DISCONNECTS, after the board has booted, so arming once at boot arms
     * it and then has it switched off - and the liveness check has already
     * passed, leaving the device reporting a counter it does not have. The
     * console read is outside every measured region. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    while (!UART_EV_RXDRDY) {}
    UART_EV_RXDRDY = 0u;
    return (int)(UART_RXD & 0xFFu);
}

/* ---- trigger ----------------------------------------------------------- */
void ssb_trig_high(void) { P0_OUTSET = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { P0_OUTCLR = (1u << TRIG_PIN); }

static void trig_init(void)
{
    P0_OUTCLR = (1u << TRIG_PIN);                      /* start low */
    P0_DIRSET = (1u << TRIG_PIN);
}

/* ---- the cycle counter -------------------------------------------------
 * SYSTICK IS THE RIGHT DEFAULT ON THIS PART AND DWT IS NOT, which is the
 * opposite of the ST parts here. The research tree measured it: on the
 * nRF52 the DWT sits in the debug power domain, and with no debugger
 * attached CYCCNT reads a frozen constant. Every interval derived from it
 * then comes out exactly zero, which reads as a constant-time result rather
 * than as a missing counter.
 *
 * So the DWT arm below keeps the liveness check and reports its outcome
 * honestly through ssb_has_cyc, and on this part that check is expected to
 * FAIL and the build to report no counter. That is the contract working:
 * SS_V_ENC_CYC then answers with an error and the host learns it from the
 * device. Build with TIMING=systick, which is the default. */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                 /* counts UP over 32 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    DEMCR |= DEMCR_TRCENA;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    __asm volatile("dsb; isb");
    a = DWT_CYCCNT;
    for (volatile int i = 0; i < 64; i++) {}
    b = DWT_CYCCNT;
    g_cyc_ok = (b != a);                   /* report what is true */
}

#elif defined(SS_TIMING_SYSTICK)
int      ssb_has_cyc(void)  { return 1; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    /* THE BOARD OWNS THE SUBTRACTION, and the direction is the reason the
     * hook exists. SysTick counts DOWN, so the later read is the smaller
     * one and the difference is first - second; the mask is 24 bits because
     * that is the whole counter, and it is what makes one wrap come out
     * right instead of as a huge positive number. */
    return (first - second) & 0xFFFFFFu;
}

static void cyc_init(void)
{
    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0u;
    SYST_CSR = 5u;                         /* ENABLE | core clock */
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

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

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
