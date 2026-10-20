/* B-L475E-IOT01A board layer (STM32L475VGT6).
 *
 * UNTESTED ON SILICON: written from rigs/fw_l4_aes/ in the research tree,
 * built, never run. What that leaves open is listed at the bottom of this
 * file.
 *
 * Same accelerator interface as the G474 and the two F4 parts - plain
 * FLASH_ACR with LATENCY, PRFTEN, ICEN, DCEN and the two reset pulses -
 * so no packing, and the G474's ICRST/DCRST dance ports over unchanged.
 * The research tree says that in terms, which is also why an ACR value
 * here means what the same value means on that part.
 *
 * What the research tree already establishes about this die, which is why
 * the accelerator list in board.mk is ordered the way it is:
 *
 *   * the flash is read 64 bits at a time - 8 bytes - and that width is
 *     INVARIANT across all eight accelerator settings, so it is a
 *     property of the array rather than of the accelerator. A byte S-box
 *     wants line_shift 3; see eval/mcu/stm32l475vg/eval.mk.
 *   * there is a 1-entry 8-byte read latch under the cache that NO
 *     FLASH_ACR BIT REACHES. DCEN only MASKS it. So "accelerator off" is
 *     not "no data reuse" on this part.
 *   * the reset default LEAKS. FLASH_ACR comes up 0x600 and ST's own
 *     factory image on this board runs 0x604.
 *   * TURNING THE ACCELERATOR OFF IS THE OPPOSITE OF A FIX. 0x004 is the
 *     cheapest cell to attack of any measured here (4 000 traces) while
 *     the reset default needs >275 000 - and 0x504 carries the LARGEST
 *     standard deviation of the whole surface while yielding 0/16 keys at
 *     100 000. A statistic is not a channel, and this part is where that
 *     is most visibly true.
 *   * tables in SRAM1 are the only complete fix measured.
 *
 * Clock is HSI16 at 16 MHz, to match the F429/G474/L552 rigs so a cycle
 * count here is comparable with theirs. THE RESET CLOCK IS NOT THAT: see
 * clock_init.
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
#  error "stm32l475vg offers LINK=vcp only - see mcufw/stm32l475vg/board.mk"
#endif

/* PB14 IS LD2, AND IT IS THE ONLY OUTPUT PIN ON THIS BOARD WHOSE IDENTITY
 * IS ESTABLISHED BY MEASUREMENT rather than by a user manual: the research
 * tree read it out of the live factory image's own GPIO and RCC registers,
 * at the same time as it found that GPIOA is not clocked there at all.
 *
 * WHAT THAT COSTS, SAID PLAINLY. An LED pin is a marker, not a
 * laboratory-grade edge: the LED and its series resistor load the net, so
 * the fall time is worse than a bare header pin's. It is fine for the arm
 * this repository actually builds - the cycle counter is the instrument and
 * the trigger is for a scope to find the window - and it is NOT the pin to
 * take an off-chip capture against. A header pin from UM2153 would be
 * better and is deliberately not guessed at here: picking one out of a
 * user manual is exactly what produced the PA5-is-the-LED error that the
 * factory-image read above corrected. Measure one on the board, then
 * change this line and this paragraph together.
 */
#define TRIG_PIN 14u

const char *ssb_name(void) { return "stm32l475vg"; }

/* ---- FLASH_ACR --------------------------------------------------------
 * TRAP 1: refuse what the clock cannot run, rather than execute the next
 * instruction fetch under it. Three ways a word can be illegal here, and
 * the second is this part's own:
 *
 *   * LATENCY below what the clock needs. At 16 MHz in Range 1 the
 *     minimum is zero, so this never fires at the default - it is kept
 *     because the clock is a build-time choice and under-programming does
 *     not fault, it just stops the part.
 *   * LATENCY ABOVE 4, WHICH IS THIS PART'S TRAP AND NOT THE G474'S. The
 *     field is three bits wide and only 0..4 are meaningful, so 8..15
 *     TRUNCATE: a host asking for LATENCY 8 would have the device run at
 *     LATENCY 0 and the capture record 8. Refused as a whole word, so the
 *     readback shows the host its request did not take. 5..7 are refused
 *     with it - they fit the field and are not meaningful values, and a
 *     cell nobody can name is not worth measuring.
 *   * any bit outside FLASH_ACR_WRITABLE. The two cache-reset pulses are
 *     part of the sequence below rather than a state a host can ask for,
 *     and RUN_PD/SLEEP_PD are not accelerator knobs. Refused rather than
 *     masked away, so a host typo comes back as a refusal instead of
 *     being recorded under a configuration the device never ran.
 *
 * TRAP 2: ICRST and DCRST are PULSES, writable only while their cache is
 * disabled. Writing the enable bit alone leaves whatever lines were
 * already there, so a run-time configuration is not the one a reset-time
 * write would produce - and that difference shows up as a measurement
 * rather than as an error. RM0351 section 3.3.3 is the rule; the research
 * tree's rig does the same dance and records it as a step that cost a run.
 */
static uint32_t min_latency(void)
{
    /* RM0351 Table 9, VOS Range 1, which clock_init pins. ONLY THE FIRST
     * ROW IS CORROBORATED ON THIS DIE: the research tree ran LATENCY 0
     * cells at 16 MHz, so zero wait states at 16 MHz is measured rather
     * than inferred. The rest is the reference manual's table and is
     * reached only by raising SS_CLK_MHZ, which nothing here does. */
    return (SS_CLK_HZ <= 16000000u) ? 0u
         : (SS_CLK_HZ <= 32000000u) ? 1u
         : (SS_CLK_HZ <= 48000000u) ? 2u
         : (SS_CLK_HZ <= 64000000u) ? 3u : 4u;
}

static int acr_legal(uint32_t v)
{
    uint32_t lat = v & FLASH_ACR_LATENCY_Msk;

    if (v & ~(uint32_t)FLASH_ACR_WRITABLE)
        return 0;
    if (lat > FLASH_ACR_LATENCY_MAX)
        return 0;
    if (lat < min_latency())
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

/* READ IT, never reconstruct it from what was requested. That is what
 * makes a refused configuration visible instead of being recorded under
 * the name it was asked for. */
uint32_t ssb_acr_get(void) { return FLASH_ACR; }
uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }

/* The clock is a build-time choice here: changing it at run time would
 * move the UART divisor under a live link. Reported as unavailable rather
 * than faked. */
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* No hardware hit/miss counters on this part. The L552's ICACHE has them
 * and this family's ART does not, so this returns 0 and the host reports
 * "none" rather than inventing numbers. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------
 * THE RESET CLOCK ON THIS FAMILY IS MSI AT 4 MHz AND HSI16 IS OFF, where
 * the F4, G4 and L5 parts here all reset onto a 16 MHz HSI. Two
 * consequences, and the second is the one that hangs:
 *
 *   * HSI16 has to be STARTED and waited for, then SW switched and SWS
 *     polled. Switching SW to HSI16 without starting it first waits on
 *     the SWS poll forever.
 *   * the accelerator is programmed BEFORE the switch, because this is an
 *     upward transition and the wait states have to be in place before
 *     the faster clock is. At 16 MHz in Range 1 the minimum is zero so
 *     nothing actually has to be raised, but the order is the one that
 *     stays correct if SS_CLK_MHZ is ever raised.
 *
 * PWR_CR1.VOS is pinned to Range 1 explicitly even though that IS the
 * reset default. It is not an accelerator knob (regs.h says why at
 * length) but min_latency() is only true in Range 1, so writing it is
 * what makes the refusal rule true by construction rather than by
 * assuming a reset value nothing checked.
 */
static void clock_init(void)
{
    RCC_APB1ENR1 |= RCC_APB1ENR1_PWR;
    (void)RCC_APB1ENR1;
    while (PWR_SR2 & PWR_SR2_VOSF) {}
    PWR_CR1 = (PWR_CR1 & ~PWR_CR1_VOS_Msk) | PWR_CR1_VOS_RANGE1;
    while (PWR_SR2 & PWR_SR2_VOSF) {}

    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}

    /* BOOT INTO THIS PART'S OWN RESET DEFAULT, 0x600 - both caches on,
     * prefetch off, LATENCY 0. Deliberately a SHIPPED configuration
     * rather than one invented for the bench: it is what the part comes
     * up in (measured off the live factory image, not read off a
     * datasheet), it is within one bit of what ST's own demo runs on this
     * board, and the research tree measures it as a cell that leaks. It
     * goes through ssb_acr_set, so the boot state and a host-set state
     * come from identical code - including the cache-reset dance.
     *
     * min_latency() is OR-ed in rather than LATENCY 0 being hardcoded, so
     * that this line stays legal if SS_CLK_MHZ is raised. At the 16 MHz
     * default it contributes zero and the word is exactly 0x600. */
    ssb_acr_set(min_latency() | FLASH_ACR_ICEN | FLASH_ACR_DCEN);

    RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_HSI16;
    while ((RCC_CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_HSI16) {}
}

/* ---- the link ---------------------------------------------------------
 * USART1 on PB6 (TX) / PB7 (RX), AF7, to the ST-LINK VCP. GPIOB and
 * USART1, where every Nucleo-64 port in this tree uses GPIOA and USART2 -
 * and that is measured off this board rather than taken from its user
 * manual, which is the only reason it is right.
 */
static void uart_init(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOB;
    (void)RCC_AHB2ENR;
    RCC_APB2ENR |= RCC_APB2ENR_USART1;
    (void)RCC_APB2ENR;

    /* AFRL indexes from pin 0, so PB6 is nibble 6 and PB7 nibble 7. */
    GPIOB_MODER = (GPIOB_MODER & ~((3u << 12) | (3u << 14)))
                | (2u << 12) | (2u << 14);
    GPIOB_OSPEEDR |= (3u << 12) | (3u << 14);
    GPIOB_PUPDR = (GPIOB_PUPDR & ~(3u << 14)) | (1u << 14);   /* RX pull-up */
    GPIOB_AFRL = (GPIOB_AFRL & ~((0xFu << 24) | (0xFu << 28)))
               | (7u << 24) | (7u << 28);

    /* Pin the UART's kernel clock to HSI16 rather than to the bus clock,
     * so BRR stays correct whatever the core clock does and a clock change
     * can never turn into a console that merely looks dead. */
    RCC_CCIPR = (RCC_CCIPR & ~RCC_CCIPR_USART1SEL_Msk)
              | RCC_CCIPR_USART1SEL_HSI;

    USART1_CR1 = 0u;
    USART1_CR2 = 0u;
    USART1_CR3 = 0u;
    USART1_BRR = (16000000u + (SS_BAUD / 2u)) / SS_BAUD;   /* HSI16, fixed */
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void ssb_put(uint8_t c)
{
    while (!(USART1_ISR & USART_ISR_TXE)) {}
    USART1_TDR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-arm TRCENA HERE and not in init: a probe clears it when it
     * DISCONNECTS, which is after the board has booted, so arming once at
     * boot arms it and then has it switched off - with the liveness check
     * already passed, leaving the device reporting a counter it has not
     * got and a column of exact zeros that reads as a constant-time
     * result. The console read is outside every measured region. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif
    for (;;) {
        uint32_t s = USART1_ISR;
        /* Clear overrun rather than wedging on it: ORE latches RXNE off,
         * and the host can outrun this receiver during a resync. */
        if (s & USART_ISR_ORE) USART1_ICR = USART_ISR_ORE;
        if (s & USART_ISR_RXNE) return (int)(USART1_RDR & 0xFFu);
    }
}

/* ---- trigger ---------------------------------------------------------- */
void ssb_trig_high(void) { GPIOB_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOB_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_GPIOB;
    (void)RCC_AHB2ENR;
    GPIOB_BSRR = (1u << (TRIG_PIN + 16u));             /* start low */
    GPIOB_MODER = (GPIOB_MODER & ~(3u << (2u * TRIG_PIN)))
                | (1u << (2u * TRIG_PIN));
}

/* ---- the cycle counter ------------------------------------------------
 * TRAP 3: THE BOARD OWNS THE SUBTRACTION. SysTick counts DOWN over 24
 * bits and DWT CYCCNT counts UP over 32, so the two arms subtract in
 * opposite directions and a caller must not assume either.
 *
 * SysTick is the default because it is the instrument the research tree's
 * G474, L552 and L475 rigs all use, and this part exists here to be
 * compared against those. At 16 MHz 24 bits wraps every 1.05 s against an
 * AES-128 of a few thousand cycles, so there is ample headroom.
 */
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
    /* CYCCNT CAN STOP WHILE CYCCNTENA STILL READS SET, and every derived
     * interval then comes out exactly zero. Check that it moves at all,
     * and report what is true rather than what was enabled. */
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
/* The arm with no instrumentation in it, for an externally timed
 * measurement. SS_V_ENC_CYC then answers with the error response rather
 * than with zeros, so the host learns it from the device instead of
 * reading a column of zeros as a result. */
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

/* ---- WHAT BEING UNTESTED ON SILICON LEAVES OPEN -----------------------
 * Steps 4 to 7 of docs/PORTING.md's checklist all need the board, and
 * none of them has been run. In the order they would fail:
 *
 *   * the clock. The MSI-to-HSI16 switch above is the one sequence here
 *     with no counterpart in any other port in this tree, and the
 *     research tree's own rig note says a frequency derived from
 *     hardware/boards.py for this board reads 4 rather than 16. TIME THE
 *     CLOCK rather than trusting the divider arithmetic; a UART that
 *     answers at 115200 does not prove the switch happened, because the
 *     kernel clock is pinned to HSI16 and BRR is therefore right either
 *     way. That is a deliberate trade: it protects the console at the
 *     cost of removing the console as evidence about the core clock.
 *   * the trigger pin, which is the LED (see TRIG_PIN above).
 *   * whether the LATENCY refusal fires where it should. The truncation
 *     it guards against is the F303's kind of failure - a quiet wrong
 *     number - so the check worth running is: ask for LATENCY 8, confirm
 *     the SS_V_SET_ACR readback comes back as the OLD word and not as 8
 *     and not as 0.
 *   * the RAM twin. On this part SRAM1 is the only complete fix measured,
 *     so the twin is the headline control and not just a sanity check; it
 *     must be bit-exactly constant.
 */
