/* NUCLEO-L552ZE-Q board layer.
 *
 * THIS PORT HAS NEVER RUN ON SILICON. It was written against the three
 * L5 rigs in the research repository and the in-tree CMSIS header, it
 * compiles, and nothing below has been seen to execute on a part. Every
 * number it produces should be treated as unverified until someone has
 * walked docs/PORTING.md's checklist on a board. The places where the
 * rigs did not answer a question are marked NOT ESTABLISHED rather than
 * filled in with something plausible.
 *
 * What the rigs did answer, and where:
 *
 *   flash-accelerator-tests/rigs/fw_l5_arb/board.c    the accelerator
 *       word's packing, the ICACHE reconfiguration sequence, the console
 *       (LPUART1 on PG7/PG8 AF8, clocked from HSI16, with PWR_CR2.IOSV),
 *       the HSI16 clock bring-up and the SysTick setup.
 *   flash-accelerator-tests/rigs/fw_l5_cache/main.c   the same sequence
 *       again, plus the memory-system characterisation this part's
 *       dossier is built on.
 *   flash-accelerator-tests/rigs/cmsis/stm32l552xx.h  every address and
 *       bit position, which regs.h repeats with its arithmetic shown.
 *
 * WHAT MAKES THIS PART DIFFERENT FROM THE OTHER ST PARTS HERE
 *
 * FLASH_ACR carries LATENCY and LVEN and nothing else. All flash
 * acceleration is the separate ICACHE peripheral, so the accelerator word
 * is packed - see regs.h, which also says why WAYSEL is in it.
 *
 * The ICACHE is UNIFIED: on Cortex-M33 the code region is reached over
 * C-AHB, so instruction fetch and flash data loads cross the same
 * structure. SRAM at 0x20000000 is on S-AHB and bypasses it, which is
 * what makes the RAM table twin a clean control here - see memory.ld.
 *
 * Underneath the cache there are TWO 64-bit flash read entries, direct
 * mapped on address bit 3, measured on this die
 * (flash-accelerator-tests/research/targets/stm32l552.md sections 2a-2d).
 * No register on this part can switch that latch off, so an "accelerator
 * off" cell here is not a "no reuse" cell. The dossier's section 7 is the
 * practical consequence: with the cache off, LATENCY 2, 4, 8 and 15 give
 * byte-identical attack ladders and LATENCY 0 gives one distinct timing
 * value over 150 000 plaintexts.
 *
 * Clock is HSI16 at 16 MHz, no PLL, which is what every L552 measurement
 * in the research tree was taken at, so the numbers stay comparable.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 16u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

/* The wait-state floor below is a MEASUREMENT taken at 16 MHz and holds
 * at no other clock. An L552 at its 110 MHz maximum needs five wait
 * states. Refuse to build a clock whose floor nobody here has measured,
 * rather than inherit this one. */
#if SS_CLK_MHZ != 16u
#  error "stm32l552ze: only the 16 MHz HSI16 clock has a measured wait-state floor"
#endif

/* LPUART1 is clocked from HSI16 directly, NOT from the core clock, so its
 * divisor does not follow SS_CLK_HZ. That is the rigs' choice and it is
 * the reason their clock sweep can divide HCLK without losing the
 * console. */
#define LPUART1_KER_HZ 16000000u

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "stm32l552ze offers LINK=vcp only - see mcufw/stm32l552ze/board.mk"
#endif

/* NOT ESTABLISHED: the trigger pin.
 *
 * None of the three L5 rigs drives a marker pin - they are all timed by
 * the on-chip counter and report over the console - so neither the rigs
 * nor the CMSIS header says which pin on this board reaches an LED or a
 * header. PB0 is used here because it is the pin every other Nucleo-144
 * port in this tree uses, which is a reason about this tree and not about
 * this board.
 *
 * What that costs if it is wrong: an externally triggered capture sees no
 * edge, which is a visible failure at the instrument rather than a wrong
 * number. The internally timed arm (SS_V_ENC_CYC) does not touch the pin
 * and is unaffected. Confirm it against the board's schematic before the
 * first scope capture, and correct this line rather than working around
 * it at the instrument. */
#define TRIG_PIN 0u

const char *ssb_name(void) { return "stm32l552ze"; }

/* ---- the accelerator: FLASH_ACR plus the ICACHE, as one word ----------
 *
 * TRAP 1, refuse what the clock cannot run. At 16 MHz on HSI16 zero wait
 * states is in spec and has been measured end to end on this die - the
 * dossier's section 7 surface sweeps LATENCY 0, 2, 4, 8 and 15 at this
 * clock and the LATENCY 0 cell runs. So the floor here is genuinely 0 and
 * the latency check below never fires at this clock. It is still written
 * down, because the floor is a property of the clock and the #error above
 * is what keeps the two from drifting apart.
 *
 * The check that does fire is the one on unknown bits. The word is
 * packed, so most of its bit space means nothing; accepting those bits
 * silently would record a configuration the device never ran.
 */
static uint32_t min_latency(void)
{
    return 0u;
}

static int acr_legal(uint32_t v)
{
    if (v & ~L5_ACR_KNOWN_Msk)
        return 0;
    if ((v & L5_ACR_LATENCY_Msk) < min_latency())
        return 0;
    return 1;
}

/* The monitors only count, so they are left enabled in every
 * configuration. ssb_has_monitors reports them and the host reads them
 * through the protocol. */
#define MON_EN (ICACHE_CR_HITMEN | ICACHE_CR_MISSMEN)

/* TRAP 2, the dance. This part has no ICRST/DCRST pair: CACHEINV is the
 * whole invalidation mechanism, it is a PULSE, and WAYSEL is write-locked
 * while the cache is enabled. So reaching a configuration means disable,
 * write the final EN/WAYSEL, invalidate IN that configuration, wait for
 * ICACHE_SR.BUSYF to clear, clear the end-of-busy flag, then pulse the
 * two monitor resets.
 *
 * The sequence is lifted from rigs/fw_l5_arb/board.c rather than
 * rewritten, because that is the version that has run on this die.
 * Skipping the invalidation is what invalidated a whole round of results
 * on the other boards in that tree. */
static void icache_apply(uint32_t en, uint32_t waysel)
{
    uint32_t cr = MON_EN
                | (en ? ICACHE_CR_EN : 0u)
                | (waysel ? ICACHE_CR_WAYSEL : 0u);

    ICACHE_CR &= ~ICACHE_CR_EN;
    __asm volatile("dsb; isb");
    ICACHE_CR = cr;
    __asm volatile("dsb; isb");
    ICACHE_CR = cr | ICACHE_CR_CACHEINV;
    while (ICACHE_SR & ICACHE_SR_BUSYF) {}
    ICACHE_FCR = ICACHE_FCR_CBSYENDF;
    ICACHE_CR = cr | ICACHE_CR_HITMRST | ICACHE_CR_MISSMRST;
    ICACHE_CR = cr;
    __asm volatile("dsb; isb");
}

void ssb_acr_set(uint32_t v)
{
    uint32_t lat = v & L5_ACR_LATENCY_Msk;

    if (!acr_legal(v)) return;

    /* Latency first, and read it back until it takes. The flash controller
     * accepts the write before the new wait-state count is in force, and
     * the rigs spin on the readback for that reason. */
    FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY_Msk) | lat;
    while ((FLASH_ACR & FLASH_ACR_LATENCY_Msk) != lat) {}

    icache_apply(v & L5_ACR_ICEN, v & L5_ACR_WAYSEL);
}

/* READ, never reconstructed from the request. A refused value is visible
 * to the host because SS_V_SET_ACR answers with this. */
uint32_t ssb_acr_get(void)
{
    uint32_t v = FLASH_ACR & FLASH_ACR_LATENCY_Msk;
    uint32_t cr = ICACHE_CR;

    if (cr & ICACHE_CR_EN)     v |= L5_ACR_ICEN;
    if (cr & ICACHE_CR_WAYSEL) v |= L5_ACR_WAYSEL;
    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }

/* Not implemented, and not because the part cannot do it: the rigs divide
 * HCLK with RCC_CFGR.HPRE to reach 8, 4, 2 and 1 MHz, and the console
 * survives it because LPUART1 is clocked from HSI16 rather than from the
 * core. Wiring it up here would also have to carry the new rate into
 * ssb_clk_hz and into every cycle number derived from SysTick, and none
 * of that has been exercised. Returning 0 makes the host treat a clock
 * change as unavailable rather than as an error. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* ---- the hardware hit and miss counters -------------------------------
 * This is the one part in this tree that has them, and they are the
 * reason its cache geometry was counted rather than inferred. MMONR is
 * 16 bits wide and saturates; HMONR is 32. The host is handed both raw,
 * so a saturated miss count is visible as 65535 rather than smuggled in
 * as a small number. */
int ssb_has_monitors(void) { return 1; }

void ssb_monitors_reset(void)
{
    uint32_t cr = ICACHE_CR;

    ICACHE_CR = cr | ICACHE_CR_HITMRST | ICACHE_CR_MISSMRST;
    ICACHE_CR = cr;
    __asm volatile("dsb");
}

void ssb_monitors_read(uint32_t *hit, uint32_t *miss)
{
    *hit  = ICACHE_HMONR;
    *miss = ICACHE_MMONR;
}

/* ---- clock ------------------------------------------------------------
 * The part comes out of reset on MSI. Raise the wait states before
 * switching to the faster source, switch, then put the accelerator into
 * its boot configuration through the same path the host will use, so the
 * boot state and a host-set state are produced by identical code.
 *
 * The pre-switch LATENCY of 4 is over-programming on purpose: it is what
 * the rigs write at this point, it is legal at every clock this part
 * reaches, and over-programming costs cycles where under-programming
 * changes how the next instruction fetch is served.
 */
static void clock_init(void)
{
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}

    FLASH_ACR = (FLASH_ACR & ~FLASH_ACR_LATENCY_Msk) | 4u;
    while ((FLASH_ACR & FLASH_ACR_LATENCY_Msk) != 4u) {}

    RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW_Msk)
             | (RCC_CFGR_SW_HSI16 << RCC_CFGR_SW_Pos);
    while (((RCC_CFGR & RCC_CFGR_SWS_Msk) >> RCC_CFGR_SWS_Pos)
           != RCC_CFGR_SW_HSI16) {}

    /* LATENCY 4, cache on, 2-way: the configuration the rigs call this
     * part's default, so a capture taken without setting the accelerator
     * is taken in the state the research tree's L552 numbers were. */
    ssb_acr_set(4u | L5_ACR_ICEN | L5_ACR_WAYSEL);
}

/* ---- the link ---------------------------------------------------------
 * LPUART1 on PG7 (TX) and PG8 (RX), AF8, which is the ST-LINK virtual COM
 * port on this board. PORT G READS AS ABSENT UNTIL PWR_CR2.IOSV IS SET -
 * it is on the VDDIO2 supply - and that needs the PWR clock first.
 *
 * LPUART's divisor is 256 times the kernel clock over the baud rate, not
 * the kernel clock over the baud rate.
 */
static void uart_init(void)
{
    RCC_APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC_APB1ENR1;
    PWR_CR2 |= PWR_CR2_IOSV;
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOGEN;
    (void)RCC_AHB2ENR;
    RCC_APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;
    (void)RCC_APB1ENR2;

    RCC_CCIPR1 = (RCC_CCIPR1 & ~RCC_CCIPR1_LPUART1SEL_Msk)
               | (RCC_CCIPR1_LPUART1SEL_HSI16 << RCC_CCIPR1_LPUART1SEL_Pos);

    /* AFRL indexes from pin 0 and AFRH from pin 8, so PG7 is AFRL nibble 7
     * and PG8 is AFRH nibble 0. */
    GPIOG_MODER = (GPIOG_MODER & ~((3u << (7 * 2)) | (3u << (8 * 2))))
                | (2u << (7 * 2)) | (2u << (8 * 2));
    GPIOG_OSPEEDR |= (3u << (7 * 2)) | (3u << (8 * 2));
    GPIOG_PUPDR = (GPIOG_PUPDR & ~((3u << (7 * 2)) | (3u << (8 * 2))))
                | (1u << (8 * 2));
    GPIOG_AFRL = (GPIOG_AFRL & ~(0xFu << (7 * 4))) | (8u << (7 * 4));
    GPIOG_AFRH = (GPIOG_AFRH & ~(0xFu << (0 * 4))) | (8u << (0 * 4));

    LPUART1_CR1 = 0u;
    LPUART1_CR2 = 0u;
    LPUART1_CR3 = 0u;
    LPUART1_BRR = (uint32_t)(((uint64_t)256u * LPUART1_KER_HZ
                              + (SS_BAUD / 2u)) / SS_BAUD);
    LPUART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ssb_put(uint8_t c)
{
    while (!(LPUART1_ISR & USART_ISR_TXE)) {}
    LPUART1_TDR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-arm here rather than in init: a debug probe clears DEMCR.TRCENA
     * when it DISCONNECTS, after the board has booted, so arming once at
     * boot arms it and then has it switched off - and the liveness check
     * has already passed, leaving the device reporting a counter it does
     * not have. The console read is outside every measured region.
     *
     * No lock-access write here. ARMv8-M's DWT has no LAR, only a
     * read-only LSR, so the M7 ports' unlock has no counterpart. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    while (!(LPUART1_ISR & USART_ISR_RXNE)) {}
    return (int)(LPUART1_RDR & 0xFFu);
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOB_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOB_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC_AHB2ENR;
    GPIOB_BSRR = (1u << (TRIG_PIN + 16u));             /* start low */
    GPIOB_MODER = (GPIOB_MODER & ~(3u << (2u * TRIG_PIN)))
                | (1u << (2u * TRIG_PIN));
}

/* ---- the cycle counter ------------------------------------------------
 * TRAP 3: the board owns the subtraction, because the two counters run in
 * opposite directions.
 *
 * SysTick is the arm the L5 rigs use, so it is the one with silicon
 * behind it. The DWT arm is the one in doubt: rigs/fw_l5_cache/main.c
 * carries a command whose whole purpose is to ask whether CYCCNT works on
 * this part, and nothing in the dossier records the answer. So the DWT
 * build here reports liveness from a measurement rather than asserting
 * it, which is what ssb_has_cyc is for.
 */
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
