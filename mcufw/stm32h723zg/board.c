/* NUCLEO-H723ZG board layer.
 *
 * THE THIRD PART, and the one that actually stretches the board contract
 * instead of confirming it. The first two are a one-bit latency field and
 * a four-bit one with two vendor caches behind FLASH_ACR. This part has
 * neither: FLASH_ACR holds LATENCY and WRHIGHFREQ and nothing else, every
 * cache on the data path is the Cortex-M7 L1 in the core's own SCB, and
 * the ITCM-side ART does not reach that path at all. The contract's
 * answer to this is "pack the fields into one word and document the
 * packing in the board's own header", which is what regs.h does - so the
 * protocol layer still sees one opaque accelerator word and still knows
 * nothing about what kind of part it is talking to.
 *
 * Three more things are different here and all three are load-bearing:
 *
 *   * the flash is read 256 bits at a time - 32 bytes, measured - so the
 *     attack wants line_shift 5 for a byte S-box where the F429 wants 4
 *     and the L152 wants 3. Nothing on the chip reports this; see
 *     eval/mcu/stm32h723zg/eval.mk.
 *   * the only RAM that is a valid twin is DTCM, because AXI SRAM is
 *     behind the D-cache. See memory.ld.
 *   * the part boots SLOW AND UNCACHED - seven wait states, both L1 caches
 *     off - where the F4/G4 boot fast and uncached. Every configuration
 *     here is reached from that state.
 *
 * Clock is the reset state: HSI at 64 MHz, no PLL, no prescalers. That is
 * what the existing measurements of this part were taken at, so the
 * numbers stay comparable, and it keeps the clock out of the set of things
 * that changed between parts.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 64u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "stm32h723zg offers LINK=vcp only - see mcufw/stm32h723zg/board.mk"
#endif

/* PB0 is LD1, the green user LED, and is also on CN10. Visible by eye,
 * which is worth something during bring-up. */
#define TRIG_PIN 0u

const char *ssb_name(void) { return "stm32h723zg"; }

/* ---- the accelerator, which is three registers pretending to be one ---
 *
 * TRAP 1 from ss_board.h, in its sharpest form on this part: the H7 boots
 * at LATENCY = 7 and the minimum this clock can actually run is NOT
 * measured here. Under-programming flash latency does not fault, it
 * changes how the next instruction fetch is served, and on the first part
 * in this tree that stopped the device dead mid-script with four checks
 * already passed. So the floor is the RESET value, read once at boot and
 * never gone below. Over-programming is legal and harmless.
 *
 * That costs the latency axis on this part, deliberately. The accelerator
 * axis here is the two L1 caches, which is where the interesting result
 * is anyway: I-cache on with D-cache off is what production H7 firmware
 * ships, and it is the worst cell on the part. Someone who wants the
 * latency axis should measure the wait-state table for this voltage
 * scaling first and put the result here, not infer it from a datasheet
 * row they did not test.
 */
static uint32_t g_latency_floor = 7u;

static int acr_legal(uint32_t v)
{
    return (v & H7_ACR_LATENCY_Msk) >= g_latency_floor;
}

/* Clean and invalidate the whole D-cache by set/way, then disable it.
 * Hand-written rather than the CMSIS helper: an early build of the H7 rig
 * in the research tree died on its first cache reconfiguration with an
 * imprecise bus error, the hand-written path was stable, and when the
 * CMSIS path was later re-tested piece by piece none of it faulted. So
 * the cause of that fault is NOT identified. This is kept because it is
 * the version that has run, which is a weaker reason than understanding
 * it, and it is written down as such rather than dressed up.
 */
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
    /* INVALIDATE, not clean-invalidate: the cache is off, so its lines
     * hold whatever a previous configuration left and none of it is
     * authoritative. Cleaning that back to memory would write stale data
     * over good. */
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
        SCB_ICIALLU = 0u;                  /* invalidate before enabling */
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
    if (!acr_legal(v)) return;

    /* Caches down first, from whatever they were, so every configuration
     * is reached from the same state rather than from its predecessor.
     * TRAP 2: leaving a cache enabled and just writing the other bit
     * leaves its lines resident, and the configuration you measure is
     * then not the one a reset-time write would produce. */
    dcache_disable();
    icache_set(0);

    FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY_Msk)
              | (v & H7_ACR_LATENCY_Msk);
    __asm volatile("dsb; isb");

    if (v & H7_ACR_IC) icache_set(1);
    if (v & H7_ACR_DC) dcache_enable();
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void)
{
    uint32_t v = FLASH_ACR & FLASH_ACR_LATENCY_Msk;

    if (SCB_CCR & SCB_CCR_IC) v |= H7_ACR_IC;
    if (SCB_CCR & SCB_CCR_DC) v |= H7_ACR_DC;
    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The M7 L1 has no architectural hit/miss counters. Saying 0 makes the
 * host report "none" instead of inventing numbers. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------
 * Reset state, left alone: HSI at 64 MHz with HSIDIV = /1 and no PLL. The
 * only thing asserted is that the divider really is /1, because the UART
 * divisor below depends on it and a wrong baud reads as a dead board.
 */
static void clock_init(void)
{
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}
    RCC_CR &= ~RCC_CR_HSIDIV_Msk;              /* /1 */
    while (!(RCC_CR & RCC_CR_HSIDIVF)) {}
    g_latency_floor = FLASH_ACR & FLASH_ACR_LATENCY_Msk;
}

/* ---- the link --------------------------------------------------------- */
static void uart_init(void)
{
    RCC_AHB4ENR |= RCC_AHB4ENR_GPIODEN;
    (void)RCC_AHB4ENR;
    RCC_APB1LENR |= RCC_APB1LENR_USART3EN;
    (void)RCC_APB1LENR;

    /* PD8 = TX, PD9 = RX, AF7, which is the Nucleo-144's virtual COM port.
     * AFRH indexes from pin 8, so PD8 is nibble 0 and PD9 nibble 1. */
    GPIOD_MODER = (GPIOD_MODER & ~((3u << 16) | (3u << 18)))
                | (2u << 16) | (2u << 18);
    GPIOD_OSPEEDR |= (3u << 16) | (3u << 18);
    GPIOD_PUPDR = (GPIOD_PUPDR & ~((3u << 16) | (3u << 18)))
                | (1u << 16) | (1u << 18);
    GPIOD_AFRH = (GPIOD_AFRH & ~0xFFu) | (7u << 0) | (7u << 4);

    USART3_CR1 = 0u;
    USART3_CR2 = 0u;
    USART3_CR3 = 0u;
    /* USART3 runs off the APB1 kernel clock, which at reset is HCLK with
     * no prescalers, so the divisor is the core clock over the baud. */
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
    /* Re-arm here, for TWO reasons on this part where the F429 had one.
     *
     * A debug probe clears DEMCR.TRCENA when it disconnects, after the
     * board has booted, so arming once in ssb_init arms it and then has it
     * switched off - and the liveness check runs before that and passes,
     * leaving the device reporting a counter it does not have.
     *
     * And on this die the DWT is not ready out of reset at all: writes to
     * DEMCR and DWT_CTRL made from early init are silently dropped and
     * read back as zero, while the identical writes made later from a
     * command handler work every time. No debugger is involved. The
     * console read is late by construction, so it fixes both.
     */
    if (!(CORE_DEMCR & CORE_DEMCR_TRCENA)) {
        CORE_DEMCR |= CORE_DEMCR_TRCENA;
        DWT_LAR = DWT_LAR_KEY;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    while (!(USART3_ISR & USART_ISR_RXNE)) {}
    return (int)(USART3_RDR & 0xFFu);
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOB_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOB_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHB4ENR |= RCC_AHB4ENR_GPIOBEN;
    (void)RCC_AHB4ENR;
    GPIOB_BSRR = (1u << (TRIG_PIN + 16u));             /* start low */
    GPIOB_MODER = (GPIOB_MODER & ~(3u << (2u * TRIG_PIN)))
                | (1u << (2u * TRIG_PIN));
}

/* ---- the cycle counter ------------------------------------------------
 * TIMING=systick  the default here, and not for symmetry with the other
 *                 parts: SysTick is in the always-available core block,
 *                 is ready at reset, needs no enable dance, and has the
 *                 same one-cycle resolution. The DWT on this die needs
 *                 the late re-arm in ssb_get above before it counts at
 *                 all. Where both have been run on one fixed loop they
 *                 agreed to 0.02 %.
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
    /* Report what is true, not what was asked for. A counter that does
     * not move makes every interval exactly zero, which is identical to a
     * constant-time cipher in every summary statistic. */
    g_cyc_ok = (b != a);
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
