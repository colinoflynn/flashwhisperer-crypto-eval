/* NUCLEO-G474RE board layer.
 *
 * Same accelerator interface as the two F4 parts - plain FLASH_ACR with
 * LATENCY, PRFTEN, ICEN, DCEN and the two reset pulses - so no packing.
 * Everything around it moves: RCC at 0x40021000, FLASH at 0x40022000,
 * GPIO on AHB2, APB1ENR1/AHB2ENR rather than APB1ENR/AHB1ENR, HSI16 at
 * RCC_CR bit 8 rather than bit 0, and a USART with a kernel-clock select.
 *
 * What the research tree already establishes about this part, which is
 * why its accelerator list is ordered the way it is:
 *
 *   * the flash is read 64 bits at a time - 8 bytes - so a byte S-box
 *     wants line_shift 3, the narrowest of the parts here.
 *   * DCEN is the knob that matters. The ART's I-cache and the prefetcher
 *     give no data reuse at all; the 256-byte D-cache does.
 *   * the chip's DEFAULT has DCEN set, and a compact T-table leaks there.
 *     With DCEN = 0 a byte S-box still gives 12-15/16.
 *   * LATENCY = 0 with the D-cache off is bit-exact AND is the correct
 *     setting at 16 MHz, so the quiet configuration is not a slow one.
 *     Tables in SRAM are the fastest placement as well as the safe one.
 *
 * Clock is the reset state: HSI16, no PLL, as on the other parts here.
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
#  error "stm32g474re offers LINK=vcp only - see mcufw/stm32g474re/board.mk"
#endif

/* PA5 is LD2, the green user LED, and D13 on the Arduino header. */
#define TRIG_PIN 5u

const char *ssb_name(void) { return "stm32g474re"; }

/* ---- FLASH_ACR --------------------------------------------------------
 * TRAP 1: refuse what the clock cannot run. RM0440 Table 9 puts the
 * boundary for VOS range 1 at 30 MHz, so at 16 MHz every LATENCY value
 * is legal and this never fires. It is kept because the clock is a
 * build-time choice and under-programming does not fault.
 *
 * TRAP 2: ICRST and DCRST are pulses, writable only while their cache is
 * disabled. Writing the enable bit alone leaves whatever lines were
 * there, so a run-time configuration is not the one a reset-time write
 * would produce - and the difference is a measurement, not an error.
 */
static uint32_t min_latency(void)
{
    /* RM0440 Table 9, VOS range 1 (the reset default). */
    return (SS_CLK_HZ <= 30000000u) ? 0u
         : (SS_CLK_HZ <= 60000000u) ? 1u
         : (SS_CLK_HZ <= 90000000u) ? 2u
         : (SS_CLK_HZ <= 120000000u) ? 3u : 4u;
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
    /* HSI16 is the reset source and CFGR's SW field already selects it.
     * The accelerator is set through the same path the host will use, so
     * the boot state and a host-set state come from identical code. */
    ssb_acr_set(min_latency() | FLASH_ACR_PRFTEN
                | FLASH_ACR_ICEN | FLASH_ACR_DCEN);
}

/* ---- the link --------------------------------------------------------- */
static void uart_init(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOA;
    (void)RCC_AHB2ENR;
    RCC_APB1ENR1 |= RCC_APB1ENR1_USART2;
    (void)RCC_APB1ENR1;

    /* PA2 = TX, PA3 = RX, AF7. AFRL indexes from pin 0, so PA2 is nibble
     * 2 and PA3 nibble 3. */
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 4) | (3u << 6)))
                | (2u << 4) | (2u << 6);
    GPIOA_OSPEEDR |= (3u << 4) | (3u << 6);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 6)) | (1u << 6);
    GPIOA_AFRL = (GPIOA_AFRL & ~((0xFu << 8) | (0xFu << 12)))
               | (7u << 8) | (7u << 12);

    /* PIN THE UART TO HSI16 rather than to the bus clock. The G4's USART
     * has a kernel-clock select the F4's does not, and taking it means
     * BRR stays correct no matter what the core clock is doing - so a
     * clock change can never turn into a console that looks dead. */
    RCC_CCIPR = (RCC_CCIPR & ~RCC_CCIPR_USART2SEL_Msk)
              | RCC_CCIPR_USART2SEL_HSI;

    USART2_CR1 = 0u;
    USART2_CR2 = 0u;
    USART2_CR3 = 0u;
    USART2_BRR = (16000000u + (SS_BAUD / 2u)) / SS_BAUD;   /* HSI16, fixed */
    USART2_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ssb_put(uint8_t c)
{
    while (!(USART2_ISR & USART_ISR_TXE)) {}
    USART2_TDR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-arm TRCENA here, not in init: a probe clears it when it
     * DISCONNECTS, after the board has booted, so arming once at boot
     * arms it and then has it switched off - with the liveness check
     * already passed, leaving the device reporting a counter it has not
     * got. The console read is outside every measured region. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    for (;;) {
        uint32_t s = USART2_ISR;
        /* Clear overrun rather than wedging on it: ORE latches RXNE off,
         * and the host can outrun this receiver during a resync. */
        if (s & USART_ISR_ORE) USART2_ICR = USART_ISR_ORE;
        if (s & USART_ISR_RXNE) return (int)(USART2_RDR & 0xFFu);
    }
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOA_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOA_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOA;
    (void)RCC_AHB2ENR;
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
