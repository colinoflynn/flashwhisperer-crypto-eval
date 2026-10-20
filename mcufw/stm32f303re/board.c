/* NUCLEO-F303RE board layer.
 *
 * UNTESTED ON SILICON. Nothing in this directory has been run on a
 * board: it was written from rigs/fw_f303_aes/ in the research tree and
 * from rigs/cmsis/stm32f303xc.h, and it is known only to build. Every
 * number below is traceable to one of those two, and where something
 * could not be established from them this file says so instead of
 * choosing a plausible value - a wrong register here produces numbers
 * rather than an error, which is the failure this tree exists to
 * prevent. The places that still need a board are collected under WHAT
 * IS NOT ESTABLISHED below.
 *
 * WHAT IS DIFFERENT ABOUT THIS PART
 *
 * The accelerator is the smallest in this tree. FLASH_ACR is LATENCY,
 * HLFCYA, PRFTBE and a read-only PRFTBS, and that is all of it: no ICEN,
 * no DCEN, no ICRST/DCRST, no ART, no CPU cache. So the cache-reset
 * dance the F4 and G4 board files do - disable, pulse the reset bits,
 * re-enable - has NO equivalent here, and its absence is a property of
 * the part rather than a gap in this file. The only flush that exists is
 * toggling PRFTBE off and on.
 *
 * HLFCYA has no counterpart on any other part in this tree. It is
 * half-cycle flash access, and it changes flash access timing directly
 * rather than through a wait state.
 *
 * Two of the three writable knobs have legality windows, and violating
 * one hangs the part instead of returning an error. The rig records all
 * three windows and enforces them, and ssb_acr_set below enforces the
 * same three. This is trap 1 in ss_board.h, and on this part it is not
 * hypothetical: the rig's own comment records a session that left HLFCYA
 * set at 8 MHz, raised the clock to 64 MHz with it still set, and had
 * the core stop fetching, recoverable only by a reflash.
 *
 * The console is USART2 on PA2/PA3 AF7, which is the Nucleo-64 ST-LINK
 * virtual COM port, and it is the F0/F3/F7-generation peripheral with
 * ISR/RDR/TDR rather than the F4's SR/DR.
 *
 * WHAT IS NOT ESTABLISHED, and matters
 *
 *   * THE TRIGGER PIN. The F303 rig drives no trigger pin at all, so
 *     "LD2 is PA5 on this board" could not be confirmed from it. PA5 is
 *     used below because the two other Nucleo-64 ports in this tree, the
 *     F401RE and the G474RE, both document LD2 on PA5 and D13 on the
 *     Arduino header. That is an inference from sibling boards, not a
 *     reading of UM1724 for this one. Check it before scoping a trigger,
 *     and check that nothing else on the F303RE variant of the board
 *     drives PA5. What IS established is the GPIOA base and the register
 *     offsets used to drive it (see regs.h).
 *   * THE BAUD DIVISOR FLOOR. The compile-time check below refuses a
 *     divisor under 16, which is the OVER8 = 0 rule carried from the
 *     L152 port. It is conservative and is not confirmed from this
 *     part's reference manual here. At the default 8 MHz and 115200 the
 *     divisor is 69, so the check does not fire.
 *   * WHICH CONFIGURATION LEAKS. Nothing. The research tree's F303
 *     dossier reports the data path as bit-exactly constant over 68 of
 *     68 cells on this die, and an instruction-path channel that is live
 *     on the same die. This port measures the data path, so the expected
 *     result here is a null - which is a result, and is exactly why the
 *     RAM twin and the recovery still have to be run rather than
 *     assumed.
 *
 * Clock is the reset state: HSI at 8 MHz with the PLL off, which is what
 * the rig boots to and what its measured cells at 0 wait states were
 * taken at.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 8u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "stm32f303re offers LINK=vcp only - see mcufw/stm32f303re/board.mk"
#endif

/* USART2 is clocked from PCLK1 (the RCC_CFGR3.USART2SW reset default,
 * which the rig relies on and does not change), and clock_init below
 * programs PPRE1 to /1 explicitly so PCLK1 is SYSCLK rather than
 * whatever a previous image left behind.
 *
 * No cast in SS_BRR: the #if below is evaluated by the preprocessor,
 * which cannot parse one. */
#define SS_BRR ((SS_CLK_HZ + (SS_BAUD / 2u)) / SS_BAUD)
#if SS_BRR < 16
#error "SS_BAUD is too high for SS_CLK_MHZ without CR1.OVER8"
#endif

/* Carried from the sibling Nucleo-64 ports, NOT confirmed from the F303
 * rig - see WHAT IS NOT ESTABLISHED above. */
#define TRIG_PIN 5u

const char *ssb_name(void) { return "stm32f303re"; }

/* ---- FLASH_ACR --------------------------------------------------------
 * TRAP 1: refuse what this clock cannot run, rather than execute the
 * next instruction fetch under it. Three separate windows on this part,
 * all three taken from the rig, which enforces the same three:
 *
 *   LATENCY below the minimum for the live clock stops the core
 *   fetching. 0 WS at or below 24 MHz, 1 WS at or below 48 MHz, 2 WS
 *   above that. Over-programming upward is always allowed and is the
 *   deliberate tool, since a wait state is a gain factor on the
 *   measurement.
 *
 *   PRFTBE may only be TOGGLED while SYSCLK is under 24 MHz. Writing the
 *   value it already holds is not a toggle, so a sweep that leaves the
 *   prefetch alone is unaffected by this at any clock.
 *
 *   HLFCYA is legal only at 8 MHz or below with the PLL off.
 *
 * TRAP 2 DOES NOT APPLY HERE, and the reason is the finding rather than
 * the omission: there are no cache enables and no cache reset bits on
 * this part, so there is no state left behind for a reset-time write to
 * differ from. The register is written in one store.
 *
 * A bit outside FLASH_ACR_WRITABLE is REFUSED rather than masked out.
 * Masking would let a host ask for DCEN on a part that has none and read
 * back a word that looks like a successful write of something else;
 * refusing leaves the readback unchanged, which is how the caller
 * learns. The one bit a readback can carry that no write put there is
 * PRFTBS, the prefetch status, and that is the register reporting
 * itself.
 */
static uint32_t min_latency(void)
{
    return (SS_CLK_HZ <= 24000000u) ? 0u
         : (SS_CLK_HZ <= 48000000u) ? 1u : 2u;
}

static int acr_legal(uint32_t v)
{
    uint32_t cur = FLASH_ACR;

    if (v & ~FLASH_ACR_WRITABLE)
        return 0;
    if ((v & FLASH_ACR_LATENCY_Msk) < min_latency())
        return 0;
    if (((v ^ cur) & FLASH_ACR_PRFTBE) && SS_CLK_HZ >= 24000000u)
        return 0;
    if ((v & FLASH_ACR_HLFCYA)
        && (SS_CLK_HZ > 8000000u || (RCC_CR & RCC_CR_PLLON)))
        return 0;
    return 1;
}

void ssb_acr_set(uint32_t v)
{
    if (!acr_legal(v)) return;

    FLASH_ACR = v;
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void) { return FLASH_ACR; }
uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }

/* The clock is a build-time choice here, as on the other parts in this
 * tree: changing it at run time would change the UART divisor under a
 * live link. Reported as unavailable rather than faked. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* No cache of any kind on this part, so no hit/miss counters to read. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------
 * HSI at 8 MHz with the PLL off, which is the reset state. The switch is
 * programmed rather than assumed, because a warm reset from another
 * image can leave the PLL running and this file would then be computing
 * its UART divisor and its minimum wait state for a clock that is not
 * the live one.
 *
 * The wait state is raised to 2 and HLFCYA cleared BEFORE the switch, and
 * lowered to this clock's minimum after it has settled. 2 is legal at
 * every frequency this part reaches, so the safe value during a
 * transition is simply the maximum. HLFCYA is cleared on the way rather
 * than preserved: a legality window is a constraint on the pair (knob,
 * clock), so it has to be enforced on both transitions, and the rig
 * records a dead board from preserving it across a clock raise.
 */
static void clock_init(void)
{
    FLASH_ACR = (FLASH_ACR & ~(FLASH_ACR_LATENCY_Msk | FLASH_ACR_HLFCYA)) | 2u;
    __asm volatile("dsb; isb");

    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY)) {}
    RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_HSI;
    while ((RCC_CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_HSI) {}
    RCC_CR &= ~RCC_CR_PLLON;
    while (RCC_CR & RCC_CR_PLLRDY) {}
    RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_PPRE1_Msk) | RCC_CFGR_PPRE1_DIV1;

    /* Through ssb_acr_set, so the boot state and a host-set state are
     * produced by identical code and the same three refusals. */
    ssb_acr_set(min_latency() | FLASH_ACR_PRFTBE);
}

/* ---- the link ---------------------------------------------------------
 * PA2 = TX, PA3 = RX, both AF7. AFRL indexes from pin 0, so PA2 is
 * nibble 2 and PA3 nibble 3, and MODER/PUPDR index two bits per pin.
 */
static void uart_init(void)
{
    RCC_AHBENR |= RCC_AHBENR_IOPAEN;
    (void)RCC_AHBENR;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC_APB1ENR;

    GPIOA_MODER = (GPIOA_MODER & ~((3u << 4) | (3u << 6)))
                | (2u << 4) | (2u << 6);
    GPIOA_OSPEEDR |= (3u << 4) | (3u << 6);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~(3u << 6)) | (1u << 6);   /* pull-up on RX */
    GPIOA_AFRL = (GPIOA_AFRL & ~((0xFu << 8) | (0xFu << 12)))
               | (7u << 8) | (7u << 12);

    USART2_CR1 = 0u;
    USART2_BRR = SS_BRR;
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
    while (!(USART2_ISR & USART_ISR_RXNE)) {}
    return (int)(USART2_RDR & 0xFFu);
}

/* ---- trigger ----------------------------------------------------------
 * Two register writes and nothing else, both to BSRR so neither is a
 * read-modify-write of ODR.
 */
void ssb_trig_high(void) { GPIOA_BSRR = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { GPIOA_BSRR = (1u << (TRIG_PIN + 16u)); }

static void trig_init(void)
{
    RCC_AHBENR |= RCC_AHBENR_IOPAEN;
    (void)RCC_AHBENR;
    GPIOA_BSRR = (1u << (TRIG_PIN + 16u));             /* start low */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << (2u * TRIG_PIN)))
                | (1u << (2u * TRIG_PIN));
}

/* ---- the cycle counter ------------------------------------------------
 * TRAP 3: the board owns the subtraction, because the two counters this
 * part offers run in opposite directions. DWT CYCCNT counts UP over 32
 * bits; SysTick counts DOWN over 24, so its delta is (first - second)
 * masked to 24 bits and a caller assuming second - first would get large
 * positive garbage rather than an error.
 *
 * ssb_has_cyc reports what is true in both arms rather than returning a
 * constant 1. A counter that does not move looks exactly like a
 * constant-time result, and on this part - whose data path the research
 * tree reports as genuinely constant - that confusion would be
 * especially hard to spot from the host.
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
    /* NOCYCCNT reads 1 on a core that implements no cycle counter at
     * all, which is a different failure from one that is merely not
     * running, so both are checked. */
    if (DWT_CTRL & DWT_CTRL_NOCYCCNT) {
        g_cyc_ok = 0;
        return;
    }
    a = DWT_CYCCNT;
    for (volatile int i = 0; i < 64; i++) {}
    b = DWT_CYCCNT;
    g_cyc_ok = (b != a);
}

#elif defined(SS_TIMING_SYSTICK)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & 0xFFFFFFu;   /* counts DOWN over 24 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0u;
    SYST_CSR = 5u;                         /* ENABLE | core clock */
    __asm volatile("dsb; isb");
    a = SYST_CVR;
    for (volatile int i = 0; i < 64; i++) {}
    b = SYST_CVR;
    g_cyc_ok = (b != a);
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
