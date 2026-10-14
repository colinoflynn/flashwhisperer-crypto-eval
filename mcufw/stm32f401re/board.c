/* NUCLEO-F401RE board layer.
 *
 * The small F4. Its accelerator is plain FLASH_ACR with the same
 * LATENCY / PRFTEN / ICEN / DCEN / ICRST / DCRST layout as the F429, so
 * no packing is needed and the word the host sets IS the register. What
 * is different is the board: a NUCLEO-64, so the console is USART2 on
 * PA2/PA3 and the LED is PA5.
 *
 * WHAT IS NOT KNOWN ABOUT THIS PART, and matters. Unlike the other three
 * it has never been measured for this channel on this bench - there is
 * no roster row and no dossier for it, only a constant-time rig. So:
 *
 *   * its FLASH READ WIDTH is not established here. The F429's is 16
 *     bytes measured, and the F401 is the same family, but "same family"
 *     is exactly the inference this project keeps catching itself out
 *     on - the F7 family moves its DTCM/SRAM1 boundary between parts.
 *     eval/mcu/stm32f401re/eval.mk therefore records the width as
 *     UNMEASURED and tells the attack to sweep rather than assume.
 *   * nothing here predicts which accelerator cell leaks. The two M7
 *     parts in this tree already disagree with each other about that.
 *
 * Clock is the reset state: HSI at 16 MHz, no PLL, which is where the
 * other ST parts here are measured so the numbers stay comparable.
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
#  error "stm32f401re offers LINK=vcp only - see mcufw/stm32f401re/board.mk"
#endif

/* PA5 is LD2, the green user LED, and is CN5 pin 6 / D13 on the Arduino
 * header. It is also SPI1 SCK, which nothing here uses. */
#define TRIG_PIN 5u

const char *ssb_name(void) { return "stm32f401re"; }

/* ---- FLASH_ACR --------------------------------------------------------
 * The same two traps as the F429, because it is the same register.
 *
 * TRAP 1: refuse what the clock cannot run. At 16 MHz on HSI every
 * LATENCY value is legal and this never fires, but the clock is a
 * build-time choice and under-programming does not fault - it changes
 * how the very next instruction fetch is served.
 *
 * TRAP 2: the cache reset bits are a PULSE and may only be written while
 * the cache is disabled. Writing the enable bit alone leaves whatever
 * lines were already there, so a configuration reached at run time is
 * not the one a reset-time write would produce, and the difference shows
 * up as a measurement rather than as an error.
 */
static uint32_t min_latency(void)
{
    /* RM0368 Table 6, VOS scale 2 (the reset default), 2.7-3.6 V.
     * The F401 tops out at 84 MHz, so this table is shorter than the
     * F429's rather than merely different. */
    return (SS_CLK_HZ <= 30000000u) ? 0u
         : (SS_CLK_HZ <= 60000000u) ? 1u : 2u;
}

static int acr_legal(uint32_t v)
{
    if ((v & FLASH_ACR_LATENCY_Msk) < min_latency())
        return 0;
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

    FLASH_ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    __asm volatile("dsb; isb");
    FLASH_ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    __asm volatile("dsb; isb");
    FLASH_ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    __asm volatile("dsb; isb");
    FLASH_ACR = (v & (FLASH_ACR_LATENCY_Msk | FLASH_ACR_PRFTEN));
    __asm volatile("dsb; isb");
    FLASH_ACR |= want_i | want_d;
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void) { return FLASH_ACR; }
uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------ */
static void clock_init(void)
{
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}
    /* HSI is the reset source and CFGR's SW field is already 0, so there
     * is nothing to switch. The accelerator is set through the same path
     * the host will use, so the boot state and a host-set state are
     * produced by identical code. */
    ssb_acr_set(min_latency() | FLASH_ACR_PRFTEN
                | FLASH_ACR_ICEN | FLASH_ACR_DCEN);
}

/* ---- the link --------------------------------------------------------- */
static void uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOA;
    (void)RCC_AHB1ENR;
    RCC_APB1ENR |= RCC_APB1ENR_USART2;
    (void)RCC_APB1ENR;

    /* PA2 = TX, PA3 = RX, AF7. AFRL indexes from pin 0, so PA2 is nibble
     * 2 and PA3 nibble 3. */
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 4) | (3u << 6)))
                | (2u << 4) | (2u << 6);
    GPIOA_OSPEEDR |= (3u << 4) | (3u << 6);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 6)) | (1u << 6);
    GPIOA_AFRL = (GPIOA_AFRL & ~((0xFu << 8) | (0xFu << 12)))
               | (7u << 8) | (7u << 12);

    USART2_CR1 = 0u;
    /* USART2 is on APB1, which at reset runs at HCLK with no prescaler. */
    USART2_BRR = (SS_CLK_HZ + (SS_BAUD / 2u)) / SS_BAUD;
    USART2_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ssb_put(uint8_t c)
{
    while (!(USART2_SR & USART_SR_TXE)) {}
    USART2_DR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-arm TRCENA here, not in init: a debug probe clears it when it
     * DISCONNECTS, after the board has booted, so arming once at boot
     * arms it and then has it switched off - and the liveness check has
     * already passed, leaving the device reporting a counter it does not
     * have. The console read is outside every measured region. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    while (!(USART2_SR & USART_SR_RXNE)) {}
    return (int)(USART2_DR & 0xFFu);
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOA_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOA_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOA;
    (void)RCC_AHB1ENR;
    GPIOA_BSRR = (1u << (TRIG_PIN + 16u));             /* start low */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << (2u * TRIG_PIN)))
                | (1u << (2u * TRIG_PIN));
}

/* ---- the cycle counter ------------------------------------------------ */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                 /* counts UP */
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
