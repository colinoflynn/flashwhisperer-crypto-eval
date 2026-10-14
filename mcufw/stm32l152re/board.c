/* STM32L152RE board layer. The whole part-specific half of a victim.
 *
 * Cortex-M3 at 32 MHz, one-bit LATENCY field, no cache of any kind: the
 * FLOOR CASE, and a good first port since nothing here can hide behind a
 * cache. Everything part-specific is in this file; the protocol loop
 * (instrumentation/victim/src/ss_victim.c) and the cipher
 * (eval/lib/<name>/provider.c) mention no part at all.
 *
 * The clock, the FLASH_ACR ordering and the trigger pin are the ones a
 * measured baseline for this part was taken with, so a reference-cipher
 * build here is directly comparable with it.
 */
#include <stdint.h>
#include "regs.h"
#include "ss_board.h"
#include "ss_victim.h"

#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 32
#endif
#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

/* No cast: `#if SS_BRR < 16` below is evaluated by the PREPROCESSOR, which
 * cannot parse one and reports it as a missing binary operator. */
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

/* BRR is 12.4 fixed point when OVER8 = 0, so the register value is fck/baud
 * rounded. USARTDIV < 1 is not representable, which is what the assertion
 * catches: at 32 MHz the ceiling without OVER8 is 2 Mbaud. Going beyond
 * needs CR1.OVER8 and is deliberately NOT done, since the oversampling
 * change moves the receiver's sampling point, and that's a
 * measurement-affecting knob that wants a control rather than a silent
 * default. */
#define SS_BRR ((SS_CLK_HZ + (SS_BAUD / 2u)) / SS_BAUD)
#if SS_BRR < 16
#error "SS_BAUD is too high for SS_CLK_MHZ without CR1.OVER8"
#endif

#if defined(SS_LINK_VCP)
#  define UART_SR   USART2_SR
#  define UART_DR   USART2_DR
#  define UART_BRR  USART2_BRR
#  define UART_CR1  USART2_CR1
#elif defined(SS_LINK_HEADER)
#  define UART_SR   USART1_SR
#  define UART_DR   USART1_DR
#  define UART_BRR  USART1_BRR
#  define UART_CR1  USART1_CR1
#else
#  error "define SS_LINK_VCP or SS_LINK_HEADER"
#endif

/* PA8 = CN9 pin 8 (`D7`) on the ARDUINO header, CN10 pin 23 on the ST
 * morpho. UM1724 Rev 17, Table 21 and Table 30. No solder bridge touches it. */
#define TRIG_PIN 8u

const char *ssb_name(void) { return "stm32l152re"; }

/* ---- FLASH_ACR --------------------------------------------------------
 * RM0038 rev 16, 3.9.1: LATENCY = 1 requires ACC64 = 1, PRFTEN = 1 requires
 * ACC64 = 1, and clearing ACC64 requires clearing both first. Going up,
 * ACC64 first and its dependants after; going down, the dependants first.
 * Each transition is fenced, since this register decides how the very next
 * instruction fetch is served.
 *
 * REFUSE what this clock cannot run, rather than execute the next fetch
 * under it. Measured the hard way: writing FLASH_ACR = 0x04 (no wait state)
 * at 32 MHz, where RM0038 makes one mandatory, stopped the part dead
 * mid-script with four checks already passed, and from the host it looked
 * like an unrelated crash. The caller learns it was refused because
 * SS_V_SET_ACR replies with the readback. This is trap 1 in ss_board.h.
 */
static int acr_legal(uint32_t v)
{
    if ((v & (FLASH_ACR_LATENCY | FLASH_ACR_PRFTEN)) && !(v & FLASH_ACR_ACC64))
        return 0;
#if SS_CLK_MHZ == 32
    if (!(v & FLASH_ACR_LATENCY))
        return 0;
#endif
    return 1;
}

void ssb_acr_set(uint32_t v)
{
    if (!acr_legal(v)) return;

    if (v & FLASH_ACR_ACC64) {
        FLASH_ACR |= FLASH_ACR_ACC64;
        __asm volatile("dsb; isb");
        FLASH_ACR = v;
    } else {
        FLASH_ACR &= ~(FLASH_ACR_LATENCY | FLASH_ACR_PRFTEN);
        __asm volatile("dsb; isb");
        FLASH_ACR = v;
    }
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void) { return FLASH_ACR; }
uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }

/* This part's clock is chosen at build time, not at run time: changing it
 * would change the UART divisor under a live link. Reported as unavailable
 * rather than faked. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* No cache, so no hit/miss counters. A part that has them says so here. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------
 * 32 MHz is the default and that matters: at 16 MHz in voltage range 1 a
 * wait state is OVER-programmed (legal, but not what the part needs), so a
 * LATENCY = 1 result there is open to "you measured a configuration nobody
 * ships". At 32 MHz RM0038's table makes one wait state MANDATORY, and
 * 32 MHz is the fastest this silicon goes.
 */
static void clock_init(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_PWR;
    while (PWR_CSR & PWR_CSR_VOSF) {}
    PWR_CR = (PWR_CR & ~PWR_CR_VOS_Msk) | PWR_CR_VOS_RANGE1;
    while (PWR_CSR & PWR_CSR_VOSF) {}

    ssb_acr_set(FLASH_ACR_ACC64 | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY);

    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}
    RCC_CFGR = (RCC_CFGR & ~3u) | 1u;                  /* SW = HSI */
    while (((RCC_CFGR >> 2) & 3u) != 1u) {}

#if SS_CLK_MHZ == 32
    RCC_CR &= ~RCC_CR_PLLON;
    while (RCC_CR & RCC_CR_PLLRDY) {}
    RCC_CFGR = (RCC_CFGR & ~((0xFu << 18) | (3u << 22) | (1u << 16)))
             | (1u << 18)                              /* PLLMUL = x4 */
             | (1u << 22);                             /* PLLDIV = /2 */
    RCC_CR |= RCC_CR_PLLON;
    while (!(RCC_CR & RCC_CR_PLLRDY)) {}
    RCC_CFGR = (RCC_CFGR & ~3u) | 3u;                  /* SW = PLL */
    while (((RCC_CFGR >> 2) & 3u) != 3u) {}
#elif SS_CLK_MHZ != 16
#  error "SS_CLK_MHZ must be 16 or 32 - the part's two in-spec points here"
#endif
}

/* ---- the link --------------------------------------------------------- */
static void uart_init(void)
{
    RCC_AHBENR |= RCC_AHBENR_GPIOA;
#if defined(SS_LINK_VCP)
    RCC_APB1ENR |= RCC_APB1ENR_USART2;
    /* PA2 = TX, PA3 = RX, AF7. SB13/SB14 are ON by default so these reach
     * the ST-LINK's virtual COM port; SB62/SB63 are OFF, so they are NOT on
     * the headers at all. UM1724 Rev 17 Table 10. */
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 4) | (3u << 6))) | (2u << 4) | (2u << 6);
    GPIOA_OSPEEDR |= (3u << 4) | (3u << 6);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 6)) | (1u << 6);
    GPIOA_AFRL = (GPIOA_AFRL & ~((0xFu << 8) | (0xFu << 12))) | (7u << 8) | (7u << 12);
#else
    RCC_APB2ENR |= RCC_APB2ENR_USART1;
    /* PA9 = TX (CN5 pin 1, `D8`), PA10 = RX (CN9 pin 3, `D2`), AF7. AFRH
     * indexes from pin 8, so PA9 is nibble 1 and PA10 nibble 2. */
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 18) | (3u << 20))) | (2u << 18) | (2u << 20);
    GPIOA_OSPEEDR |= (3u << 18) | (3u << 20);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 20)) | (1u << 20);
    GPIOA_AFRH = (GPIOA_AFRH & ~((0xFu << 4) | (0xFu << 8))) | (7u << 4) | (7u << 8);
#endif
    UART_CR1 = 0;
    UART_BRR = SS_BRR;
    UART_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ssb_put(uint8_t c)
{
    while (!(UART_SR & USART_SR_TXE)) {}
    UART_DR = c;
}

int ssb_get(void)
{
    while (!(UART_SR & USART_SR_RXNE)) {}
    return (int)(UART_DR & 0xFFu);
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOA_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOA_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHBENR |= RCC_AHBENR_GPIOA;
    GPIOA_BSRR = (1u << (TRIG_PIN + 16u));             /* start low */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << (2u * TRIG_PIN))) | (1u << (2u * TRIG_PIN));
}

/* ---- the optional cycle counter --------------------------------------
 * SysTick is a 24-bit DOWN counter at the core clock. It wraps every 0.52 s
 * at 32 MHz, far longer than one encryption.
 *
 * THE DOWN-COUNT IS WHY ssb_cyc_delta EXISTS. This was the first part
 * ported, and a caller assuming `second - first` would have produced large
 * positive garbage rather than an error. Trap 3 in ss_board.h.
 */
#ifdef SS_TIMING_SYSTICK
int      ssb_has_cyc(void)       { return 1; }
uint32_t ssb_cyc_read(void)      { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & 0xFFFFFFu;   /* counts DOWN */
}
#else
int      ssb_has_cyc(void)       { return 0; }
uint32_t ssb_cyc_read(void)      { return 0u; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    (void)first; (void)second; return 0u;
}
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
#ifdef SS_TIMING_SYSTICK
    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0;
    SYST_CSR = 5u;
#endif
}
