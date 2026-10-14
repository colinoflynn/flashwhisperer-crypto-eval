/* NUCLEO-F429ZI board layer.
 *
 * THE SECOND PART PORTED, and the one that was supposed to say whether the
 * board contract is real or whether it was shaped around the first part.
 * It differs from that part in all three places the contract has hooks
 * for, which is why it was chosen:
 *
 *   * a FOUR-BIT wait-state field and TWO CACHES, against one latency bit
 *     and no cache -- so ssb_acr_set has a real dance to do;
 *   * a cycle counter that counts UP over 32 bits (DWT CYCCNT), against
 *     one that counts DOWN over 24 (SysTick) -- so ssb_cyc_delta earns its
 *     existence rather than merely having it. BOTH are built here,
 *     selected by TIMING=, so the two directions are exercised on one part
 *     against the same cipher;
 *   * a different console peripheral on different pins.
 *
 * Nothing outside this file and its header changed to add it.
 */
#include <stdint.h>
#include "regs.h"
#include "ss_board.h"
#include "ss_victim.h"

/* HSI, 16 MHz, no PLL. Deliberate: it is the configuration the earlier
 * measurements of this part were taken at, so the numbers are comparable,
 * and it keeps the clock out of the set of things that changed between
 * parts. The accelerator is the axis here, not the clock. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 16
#endif
#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)
#define SS_BRR ((SS_CLK_HZ + (SS_BAUD / 2u)) / SS_BAUD)
#if SS_BRR < 16
#error "SS_BAUD is too high for SS_CLK_MHZ without CR1.OVER8"
#endif

/* The Nucleo-144's virtual COM port is USART3 on PD8/PD9, AF7. There is no
 * second broken-out UART wired by default on this board, so unlike the
 * other part here `header` and `vcp` are the same peripheral -- board.mk
 * offers only `vcp` and the build refuses the other rather than silently
 * producing an image wired to nothing. */
#if !defined(SS_LINK_VCP)
#  error "stm32f429zi offers LINK=vcp only -- see mcufw/stm32f429zi/board.mk"
#endif

/* PB0 is LD1, the green user LED, and is also on CN10. Usable as a scope
 * trigger and visible by eye, which is worth something during bring-up. */
#define TRIG_PIN 0u

const char *ssb_name(void) { return "stm32f429zi"; }

/* ---- FLASH_ACR: the ART accelerator -----------------------------------
 * RM0090 3.5.1. This is TRAP 2 from ss_board.h, live.
 *
 * The caches have explicit reset bits, and a reset bit may only be written
 * while its cache is DISABLED. Writing the enable bit alone leaves
 * whatever lines were already there, so a configuration reached by writing
 * ACR at run time is NOT the configuration a reset-time write would
 * produce -- and the difference shows up as a measurement rather than as
 * an error. So every transition here disables, resets, then enables.
 *
 * The wait-state field is four bits wide, so unlike a one-bit part there
 * is a real range to sweep, and over-programming is legal. What is NOT
 * legal is UNDER-programming: at a clock that needs wait states, zero is
 * refused. At 16 MHz in the default voltage range the part needs none, so
 * every value of the field is legal here and the refusal below never
 * fires -- it is kept because the clock is a build-time choice and the
 * next person to raise it should not have to remember this.
 */
static uint32_t min_latency(void)
{
    /* RM0090 Table 10, VOS range 3 (the reset default), 2.7-3.6 V. */
    return (SS_CLK_HZ <= 30000000u) ? 0u
         : (SS_CLK_HZ <= 60000000u) ? 1u
         : (SS_CLK_HZ <= 90000000u) ? 2u
         : (SS_CLK_HZ <= 120000000u) ? 3u : 4u;
}

static int acr_legal(uint32_t v)
{
    if ((v & FLASH_ACR_LATENCY_Msk) < min_latency())
        return 0;
    /* The reset bits are a pulse, not a state. A caller asking for them to
     * be LEFT set is asking for something this register does not mean, and
     * the sequence below sets them itself where they are needed. */
    if (v & (FLASH_ACR_ICRST | FLASH_ACR_DCRST))
        return 0;
    return 1;
}

void ssb_acr_set(uint32_t v)
{
    uint32_t want_i, want_d;

    if (!acr_legal(v)) return;

    want_i = v & FLASH_ACR_ICEN;
    want_d = v & FLASH_ACR_DCEN;

    /* 1. Disable both caches, whatever they were. */
    FLASH_ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    __asm volatile("dsb; isb");

    /* 2. Reset them WHILE DISABLED. This is the step that is easy to omit
     *    and that invalidates a round of results when it is. */
    FLASH_ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    __asm volatile("dsb; isb");
    FLASH_ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    __asm volatile("dsb; isb");

    /* 3. Latency and prefetch, then the caches the caller asked for. */
    FLASH_ACR = (v & (FLASH_ACR_LATENCY_Msk | FLASH_ACR_PRFTEN));
    __asm volatile("dsb; isb");
    FLASH_ACR |= want_i | want_d;
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void) { return FLASH_ACR; }
uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The ART has no architectural hit/miss counters -- unlike some parts
 * whose cache does. Saying 0 here makes the host report "none" instead of
 * inventing numbers. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------ */
static void clock_init(void)
{
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}
    /* HSI is the reset source and CFGR's SW field is already 0, so there
     * is nothing to switch. Set the accelerator through the same path the
     * host will use, so the boot state and a host-set state are produced
     * by identical code. */
    ssb_acr_set(min_latency() | FLASH_ACR_PRFTEN
                | FLASH_ACR_ICEN | FLASH_ACR_DCEN);
}

/* ---- the link --------------------------------------------------------- */
static void uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOD;
    RCC_APB1ENR |= RCC_APB1ENR_USART3;
    /* PD8 = TX, PD9 = RX, AF7. AFRH indexes from pin 8, so PD8 is nibble 0
     * and PD9 nibble 1. */
    GPIOD_MODER = (GPIOD_MODER & ~((3u << 16) | (3u << 18)))
                | (2u << 16) | (2u << 18);
    GPIOD_OSPEEDR |= (3u << 16) | (3u << 18);
    GPIOD_PUPDR = (GPIOD_PUPDR & ~(3u << 18)) | (1u << 18);
    GPIOD_AFRH = (GPIOD_AFRH & ~((0xFu << 0) | (0xFu << 4)))
               | (7u << 0) | (7u << 4);
    USART3_CR1 = 0;
    USART3_BRR = SS_BRR;
    USART3_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ssb_put(uint8_t c)
{
    while (!(USART3_SR & USART_SR_TXE)) {}
    USART3_DR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* RE-ARM TRCENA HERE, AND THIS IS NOT BELT AND BRACES.
     *
     * A debug probe clears DEMCR.TRCENA when it DISCONNECTS, which happens
     * shortly AFTER the board has booted and run its own init -- so arming
     * the counter once in ssb_init is arming it and then having it turned
     * off again. CYCCNT then stays frozen, every delta comes out exactly
     * zero, and because the liveness check in ssb_init ran BEFORE the
     * disconnect the device cheerfully reports that it has a working
     * counter. That is what it did here on the first run: 2 000 traces of
     * mean 0.000, sd 0.0000, and an IDENT saying cyc=1.
     *
     * The console read is the right place for the repair: it is where
     * this victim spends all of its idle time, it runs once per received
     * byte, and it is outside every measured region -- so the cost is not
     * in any window and the counter is live again before the command that
     * needs it has even finished arriving.
     */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    while (!(USART3_SR & USART_SR_RXNE)) {}
    return (int)(USART3_DR & 0xFFu);
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOB_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOB_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOB;
    GPIOB_BSRR = (1u << (TRIG_PIN + 16u));
    GPIOB_MODER = (GPIOB_MODER & ~(3u << (2u * TRIG_PIN)))
                | (1u << (2u * TRIG_PIN));
    GPIOB_OSPEEDR |= (3u << (2u * TRIG_PIN));
}

/* ---- the cycle counter, in BOTH directions ---------------------------
 * TIMING=dwt      DWT CYCCNT, 32 bits, counts UP. The natural choice on
 *                 this core, and the one that proves ssb_cyc_delta is a
 *                 hook rather than a formality.
 * TIMING=systick  the same 24-bit DOWN counter the other part uses, so
 *                 the two parts can be compared through an identical
 *                 measurement path.
 * TIMING=none     no counter at all: the arm with no instrumentation.
 */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;             /* counts UP; unsigned wrap is fine */
}

static void cyc_init(void)
{
    uint32_t a, b;

    /* TRCENA ARMED HERE, not once at boot somewhere else: a debug probe
     * clears it when it disconnects after flashing, and a counter that is
     * never enabled reads zero for every window -- which is exactly what a
     * constant-time result looks like. */
    DEMCR |= DEMCR_TRCENA;
    __asm volatile("dsb; isb");
    DWT_CYCCNT = 0u;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    __asm volatile("dsb; isb");

    /* AND CHECK THAT IT ACTUALLY MOVES. CYCCNT can stop while CYCCNTENA
     * still reads set, and every derived interval then comes out exactly
     * zero. Reporting has_cyc = 0 makes SS_V_ENC_CYC answer with the error
     * response, so the host learns it from the device instead of reading a
     * column of zeros as a result. */
    a = DWT_CYCCNT;
    for (volatile int i = 0; i < 64; i++) { }
    b = DWT_CYCCNT;
    g_cyc_ok = (b != a);
}

#elif defined(SS_TIMING_SYSTICK)
int      ssb_has_cyc(void)  { return 1; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & 0xFFFFFFu;   /* counts DOWN */
}

static void cyc_init(void)
{
    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0;
    SYST_CSR = 5u;
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
