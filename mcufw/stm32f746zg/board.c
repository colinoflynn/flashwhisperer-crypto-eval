/* NUCLEO-F746ZG board layer.
 *
 * THE FOURTH PART, and the hybrid of the other three. The L152 and F429
 * keep their accelerator entirely in FLASH_ACR; the H723 has no
 * accelerator bits there at all and only the Cortex-M7 L1. This part has
 * BOTH - a real FLASH_ACR with PRFTEN and ARTEN in the F4 shape, plus the
 * M7's two L1 caches in the core's SCB - so its opaque word spans two
 * peripherals and keeps FLASH_ACR's own bit positions in its low half.
 * See regs.h for the packing.
 *
 * Clock is the reset state: HSI at 16 MHz, no PLL, no prescalers, which
 * is what the existing measurements of this part were taken at.
 *
 * What to expect, from the research tree's measurements of this part:
 *
 *   * the flash is read 32 bytes at a time, same as the H723, so the
 *     attack wants line_shift 5 for a byte S-box.
 *   * the ART and the prefetcher do NOTHING to the data path: ACR 0x307
 *     against 0x007 came out byte-identical in a cipher scan, and the
 *     structural knob map found no effect in 8 matched pairs. The ART is
 *     on the ITCM alias and this victim runs from the AXI alias.
 *   * the D-cache is the knob that reaches the read latch, and what it
 *     does is REPLACE it: 32 B x 1 with the cache off becomes 128 B x 8
 *     with it on.
 *   * the DTCM twin is not a bit-exact zero here, unlike the L152's and
 *     the H723's. DTCM is not on the same fabric as the flash. A twin
 *     with a non-zero spread is still a control if its recovery is null.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 16u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "stm32f746zg offers LINK=vcp only - see mcufw/stm32f746zg/board.mk"
#endif

/* PB0 is LD1, the green user LED, and is also on CN10. */
#define TRIG_PIN 0u

const char *ssb_name(void) { return "stm32f746zg"; }

/* ---- the accelerator: FLASH_ACR and the L1 caches --------------------
 *
 * TRAP 1. At 16 MHz on HSI every LATENCY value is legal and the floor
 * below never fires, so it is kept for the same reason as on the other
 * parts: the clock is a build-time choice and the next person to raise
 * it should not have to rediscover that under-programming flash latency
 * does not fault, it changes how the next instruction fetch is served.
 *
 * TRAP 2, twice over on this part. ARTRST is a PULSE and is writable
 * only while ARTEN is 0 (RM0385 3.4.2) - the same shape as the F4's
 * ICRST/DCRST, where clearing the enable bit alone left stale lines
 * alive across a reconfiguration. And the two L1 caches have to be taken
 * down and brought back with their own maintenance, so that every
 * configuration is reached from the same state rather than from its
 * predecessor.
 */
static uint32_t min_latency(void)
{
    /* RM0385 Table 7, VOS scale 1, 2.7-3.6 V. 16 MHz needs none. */
    return (SS_CLK_HZ <= 30000000u) ? 0u
         : (SS_CLK_HZ <= 60000000u) ? 1u
         : (SS_CLK_HZ <= 90000000u) ? 2u
         : (SS_CLK_HZ <= 120000000u) ? 3u
         : (SS_CLK_HZ <= 150000000u) ? 4u
         : (SS_CLK_HZ <= 180000000u) ? 5u : 6u;
}

static int acr_legal(uint32_t v)
{
    if ((v & FLASH_ACR_LATENCY_Msk) < min_latency())
        return 0;
    /* ARTRST is a pulse, not a state. A caller asking for it to be LEFT
     * set is asking for something this register does not mean, and the
     * sequence below pulses it where it is needed. */
    if (v & FLASH_ACR_ARTRST)
        return 0;
    return 1;
}

static void dcache_disable(void)
{
    uint32_t sets, ways, s, w;

    if (!(SCB_CCR & SCB_CCR_DC)) return;
    SCB_CSSELR = 0u;                       /* level 1, data */
    __asm volatile("dsb");
    sets = ((SCB_CCSIDR >> 13) & 0x7FFFu);
    ways = ((SCB_CCSIDR >> 3) & 0x3FFu);
    SCB_CCR &= ~SCB_CCR_DC;                /* stop allocating first */
    __asm volatile("dsb; isb");
    for (s = 0; s <= sets; s++)
        for (w = 0; w <= ways; w++)
            SCB_DCCISW = ((w << 30) | (s << 5));
    __asm volatile("dsb; isb");
}

static void dcache_enable(void)
{
    uint32_t sets, ways, s, w;

    if (SCB_CCR & SCB_CCR_DC) return;
    SCB_CSSELR = 0u;
    __asm volatile("dsb");
    sets = ((SCB_CCSIDR >> 13) & 0x7FFFu);
    ways = ((SCB_CCSIDR >> 3) & 0x3FFu);
    /* INVALIDATE, not clean-invalidate: the cache is off, so whatever is
     * in its lines is from a previous configuration and none of it is
     * authoritative. Cleaning would write stale data back over good. */
    for (s = 0; s <= sets; s++)
        for (w = 0; w <= ways; w++)
            SCB_DCISW = ((w << 30) | (s << 5));
    __asm volatile("dsb; isb");
    SCB_CCR |= SCB_CCR_DC;
    __asm volatile("dsb; isb");
}

static void icache_set(int on)
{
    if (on) {
        if (SCB_CCR & SCB_CCR_IC) return;
        __asm volatile("dsb; isb");
        SCB_ICIALLU = 0u;
        __asm volatile("dsb; isb");
        SCB_CCR |= SCB_CCR_IC;
    } else {
        if (!(SCB_CCR & SCB_CCR_IC)) return;
        __asm volatile("dsb; isb");
        SCB_CCR &= ~SCB_CCR_IC;
        SCB_ICIALLU = 0u;
    }
    __asm volatile("dsb; isb");
}

void ssb_acr_set(uint32_t v)
{
    uint32_t acr = v & F7_ACR_FLASH_Msk;

    if (!acr_legal(v)) return;

    /* 1. Caches down, from whatever they were. */
    dcache_disable();
    icache_set(0);

    /* 2. The ART down and RESET while disabled, which is the only window
     *    RM0385 allows the reset bit to be written in. */
    FLASH_ACR &= ~FLASH_ACR_ARTEN;
    __asm volatile("dsb; isb");
    FLASH_ACR |= FLASH_ACR_ARTRST;
    __asm volatile("dsb; isb");
    FLASH_ACR &= ~FLASH_ACR_ARTRST;
    __asm volatile("dsb; isb");

    /* 3. Latency and the prefetcher, then the ART the caller asked for. */
    FLASH_ACR = acr & ~FLASH_ACR_ARTEN;
    __asm volatile("dsb; isb");
    if (acr & FLASH_ACR_ARTEN) FLASH_ACR |= FLASH_ACR_ARTEN;
    __asm volatile("dsb; isb");

    /* 4. And the L1 caches last. */
    if (v & F7_ACR_IC) icache_set(1);
    if (v & F7_ACR_DC) dcache_enable();
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void)
{
    uint32_t v = FLASH_ACR & F7_ACR_FLASH_Msk;

    if (SCB_CCR & SCB_CCR_IC) v |= F7_ACR_IC;
    if (SCB_CCR & SCB_CCR_DC) v |= F7_ACR_DC;
    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The M7 L1 has no architectural hit/miss counters. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------
 * Reset state, left alone: HSI at 16 MHz, no PLL. HSIRDY is bit 1 on this
 * family and bit 2 on the H7; spinning on the wrong one hangs a board
 * that is already running perfectly well.
 */
static void clock_init(void)
{
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}
}

/* ---- the link --------------------------------------------------------- */
static void uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    (void)RCC_AHB1ENR;
    RCC_APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC_APB1ENR;

    /* PD8 = TX, PD9 = RX, AF7: the Nucleo-144's virtual COM port. AFRH
     * indexes from pin 8, so PD8 is nibble 0 and PD9 nibble 1. */
    GPIOD_MODER = (GPIOD_MODER & ~((3u << 16) | (3u << 18)))
                | (2u << 16) | (2u << 18);
    GPIOD_OSPEEDR |= (3u << 16) | (3u << 18);
    GPIOD_PUPDR = (GPIOD_PUPDR & ~((3u << 16) | (3u << 18)))
                | (1u << 18);
    GPIOD_AFRH = (GPIOD_AFRH & ~0xFFu) | (7u << 0) | (7u << 4);

    USART3_CR1 = 0u;
    USART3_CR2 = 0u;
    USART3_CR3 = 0u;
    /* USART3 is on APB1, which at reset runs at HCLK with no prescaler. */
    USART3_BRR = (SS_CLK_HZ + (SS_BAUD / 2u)) / SS_BAUD;
    USART3_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ssb_put(uint8_t c)
{
    while (!(USART3_ISR & USART_ISR_TXE)) {}
    USART3_TDR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-arm TRCENA here rather than in init: a debug probe clears it
     * when it DISCONNECTS, which happens after the board has booted, so
     * arming once at boot arms it and then has it switched off. The
     * liveness check would have already passed, leaving the device
     * reporting a counter it does not have. The console read is outside
     * every measured region and runs once per received byte. */
    if (!(CORE_DEMCR & CORE_DEMCR_TRCENA)) {
        CORE_DEMCR |= CORE_DEMCR_TRCENA;
        DWT_LAR = DWT_LAR_KEY;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    for (;;) {
        uint32_t s = USART3_ISR;
        /* Clear overrun rather than wedging on it: at 16 MHz the host can
         * outrun this receiver during a resync, and ORE latches RXNE off. */
        if (s & USART_ISR_ORE) USART3_ICR = USART_ISR_ORE;
        if (s & USART_ISR_RXNE) return (int)(USART3_RDR & 0xFFu);
    }
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOB_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOB_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC_AHB1ENR;
    GPIOB_BSRR = (1u << (TRIG_PIN + 16u));             /* start low */
    GPIOB_MODER = (GPIOB_MODER & ~(3u << (2u * TRIG_PIN)))
                | (1u << (2u * TRIG_PIN));
}

/* ---- the cycle counter ------------------------------------------------
 * TIMING=systick  the default, as on the other parts: ready at reset, no
 *                 enable dance, same one-cycle resolution, counts DOWN
 *                 over 24 bits.
 * TIMING=dwt      32 bits counting UP, for the cross-check.
 * TIMING=none     no counter: the arm with no instrumentation.
 */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                 /* counts UP; wrap is fine */
}

static void cyc_init(void)
{
    uint32_t a, b;

    CORE_DEMCR |= CORE_DEMCR_TRCENA;
    DWT_LAR = DWT_LAR_KEY;
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
    return (first - second) & 0xFFFFFFu;   /* counts DOWN over 24 bits */
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
