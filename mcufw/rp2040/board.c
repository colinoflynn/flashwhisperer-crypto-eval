/* Raspberry Pi Pico (RP2040) board layer.
 *
 * THIS PORT IS UNTESTED ON SILICON. It was written against the register
 * definitions in the research tree's RP2040 rig (rigs/fw_rp2040_aes/main.c,
 * which HAS been run on this part) and the pico-sdk's register headers read
 * for their numbers, and it has been compiled and nothing more. No board was
 * attached, nothing was flashed, and no byte has been off a wire. Steps 4
 * through 7 of docs/PORTING.md's checklist - IDENT agreeing with
 * `make addr`, ciphertexts matching the reference AES, a bit-exact RAM twin,
 * and a recovery - have not been run. Treat every number this file could
 * produce as unconfirmed until they have.
 *
 * WHY THIS PART IS WORTH HAVING. It is the first part in this tree with NO
 * INTERNAL FLASH. Code and constants are fetched and read in place out of an
 * external QSPI device over a serial link, through a 16 KB unified cache that
 * instruction fetch and table reads share. Every other part here has on-die
 * parallel flash, so this is a different path rather than another
 * confirmation - and the research tree has already measured the full key off
 * it. See research/targets/rp2040.md.
 *
 * THREE THINGS IN HERE ARE NOT WHAT AN ST BOARD FILE WOULD DO.
 *
 *   1. ssb_acr_set IS RAM-RESIDENT and that is not hygiene. XIP_CTRL lives
 *      in the register file that also holds FLUSH, and writing FLUSH from
 *      code executing out of the XIP window arrives at pc=0xFFFFFFFE in a
 *      HardFault that faulted again - measured on this silicon by the rig,
 *      with the identical write from a halted debugger completing cleanly.
 *      Whether CTRL alone has the same problem was left open there and made
 *      moot by doing it from SRAM. This port does the same.
 *
 *   2. THE CLOCK STAYS AT THE CRYSTAL, 12 MHz, AND NO PLL IS PROGRAMMED.
 *      That is a deliberate choice, not an unfinished one, and it is made
 *      because of what the gain on this part actually is: the flash clock is
 *      clk_sys divided by SSI BAUDR.SCKDV, so the cost of a miss IN CORE
 *      CYCLES is a divider ratio and does not depend on clk_sys at all.
 *      Raising clk_sys to 125 MHz would multiply both sides and change
 *      nothing this project measures, at the price of a PLL and
 *      clock-mux bring-up order whose failure mode is a part that stops with
 *      no output and which cannot be checked without a board. It also keeps
 *      the flash SCLK well inside spec whichever boot2 is linked, which an
 *      untested port has no business guessing about.
 *
 *   3. ssb_set_mhz RETURNS 0. With no PLL there is one core rate, so the
 *      host is told the clock axis is not available here rather than being
 *      given a divider that changes the cipher and the flash clock together.
 *
 * The accelerator word, what is in it and what is deliberately left out, is
 * written up at the top of regs.h. The short version for a reader of only
 * this file: one writable bit (the cache enable), plus a read-only witness of
 * which boot2 is physically in flash, because boot2 is the gain knob on this
 * part and it is selected at link time.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

/* 12 MHz: the crystal on a Pico, and the only rate this port runs. See note
 * 2 above for why there is no PLL. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 12u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "rp2040 offers LINK=vcp only - see mcufw/rp2040/board.mk"
#endif

/* The console pins. GP0 is UART0 TX and GP1 is UART0 RX on function 2, which
 * is what PICO_DEFAULT_UART_TX_PIN/RX_PIN are for PICO_BOARD=pico and what
 * the research tree's rig gets by enabling the SDK's UART stdio. A Pico has
 * no on-board USB-serial bridge to the host for these, so `vcp` here means
 * the probe's CDC or a USB-serial adapter on the header, not a bridge on the
 * board - which is the only thing LINK= describes. */
#define UART_TX_PIN 0u
#define UART_RX_PIN 1u

/* The trigger pin, and THE ONE THING IN THIS FILE TAKEN FROM A BOARD
 * DOCUMENT RATHER THAN FROM A RIG. The RP2040 rig drives no trigger - it
 * times itself with SysTick - so the project has not established a pin for
 * this part. GP25 is the on-board LED on a Pico and a Pico 2, it is a plain
 * SIO pin with nothing else on it, and it is the conventional choice. If the
 * probe ends up on a different pin, or on a board where GP25 is not free
 * (a Pico W drives its LED through the radio module, not GP25), this is the
 * line to change and the only one. */
#define TRIG_PIN 25u

const char *ssb_name(void) { return "rp2040"; }

/* ---- the accelerator word ---------------------------------------------
 * RAM-RESIDENT, see note 1 at the top. `noinline` as well as the section
 * attribute: the rig records that the SDK's own not-in-flash attribute alone
 * is not enough at -O2 because a static function called once gets inlined
 * into its caller and then runs from flash anyway, silently. ssb_acr_set has
 * external linkage and is called from another translation unit, and nothing
 * here is built with -flto, so it cannot be inlined away - `noinline` is
 * belt and braces against someone adding a local caller later.
 *
 * TRAP 1, refuse what cannot be run. On an ST part this is about the clock:
 * under-programming flash latency does not fault, it changes how the next
 * instruction fetch is served. THERE IS NO LATENCY FIELD ON THIS PART, so
 * that particular failure mode does not exist. What is refusable here is a
 * word carrying bits this port does not implement - POWER_DOWN above all,
 * which would power down the cache SRAM the core is fetching through - and
 * those are refused whole, so a host that sends an ST-shaped 0x700 by
 * mistake gets the old value back rather than a silent partial write
 * recorded under the wrong name.
 *
 * TRAP 2, the reset-equivalent dance. Enabling the cache is not one bit:
 * the tag memory keeps whatever a previous configuration left in it, so a
 * state reached at run time would not be the state a reset-time write
 * produces. The dance is disable, FLUSH (which zeroes the tag memory
 * outright on this part), read FLUSH back to wait for it, then write the
 * wanted enable into a cache that is empty. The read-back of FLUSH rather
 * than a poll of STAT.FLUSH_READY is the SDK's own method and it is the one
 * used here on purpose: it cannot spin forever, and an unbounded poll inside
 * board init is the failure that presents as a board which never says hello.
 *
 * The witness bits are IGNORED on a write, not refused, so that a sweep can
 * replay a word it recorded. What comes back then carries the checksum of
 * the boot2 that is actually in flash, which is how replaying a word from a
 * different image makes itself visible. */
__attribute__((section(".ramfunc"), noinline, used))
void ssb_acr_set(uint32_t v)
{
    uint32_t ctrl;

    if (v & ~(uint32_t)RP_ACR_MASK) return;

    __asm volatile("cpsid i" ::: "memory");

    ctrl = XIP_CTRL_REG & ~(uint32_t)XIP_CTRL_EN;
    XIP_CTRL_REG = ctrl;
    __asm volatile("dsb; isb");

    XIP_FLUSH_REG = 1u;
    (void)XIP_FLUSH_REG;                   /* read back = wait for it      */
    __asm volatile("dsb; isb");

    XIP_CTRL_REG = ctrl | (v & RP_ACR_CACHE_EN);
    __asm volatile("dsb; isb");
    (void)XIP_CTRL_REG;

    __asm volatile("cpsie i" ::: "memory");
}

/* Read, never reconstructed from the request - which is what makes a refused
 * configuration visible instead of silently recorded under the name it was
 * asked for. Both halves are reads: XIP_CTRL off the register, and the boot2
 * checksum off flash. The flash read brings one line into the cache, which is
 * why this is only ever called outside a measured region. */
uint32_t ssb_acr_get(void)
{
    uint32_t crc = BOOT2_CRC;

    return (XIP_CTRL_REG & RP_ACR_CACHE_EN)
         | ((crc << RP_ACR_B2CRC_Pos) & RP_ACR_B2CRC_Msk);
}

uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* ---- the hit/miss counters ---------------------------------------------
 * This part has real ones, which most parts here do not: XIP_CTR_HIT counts
 * XIP accesses that hit and XIP_CTR_ACC counts every XIP access, both
 * saturating at 32 bits and cleared by writing any value.
 *
 * THE CONTRACT ASKS FOR hit AND miss AND THIS PART COUNTS hit AND total, so
 * the miss figure is a subtraction done here rather than a register. It is
 * exact - every access either hits or misses - and the saturation is the one
 * thing that could make it lie, so a saturated ACC is reported as a zero
 * miss count rather than as a wrapped difference.
 *
 * WHAT THESE NUMBERS COUNT THAT NOBODY WILL EXPECT: the cache is UNIFIED, so
 * ACC counts instruction fetches as well as table reads. A victim running out
 * of XIP counts itself, and most of what it counts is its own code. The
 * counters are ground truth about the cache, not about the table, and the
 * research tree's rig has to run its probes from SRAM to separate the two.
 * ACC also counts accesses made through the 0x13 bypass alias, so the bypass
 * and the counter are independent. */
int ssb_has_monitors(void) { return 1; }

void ssb_monitors_reset(void)
{
    XIP_CTR_HIT_REG = 0u;
    XIP_CTR_ACC_REG = 0u;
    __asm volatile("dsb; isb");
}

void ssb_monitors_read(uint32_t *hit, uint32_t *miss)
{
    uint32_t h = XIP_CTR_HIT_REG;
    uint32_t a = XIP_CTR_ACC_REG;

    *hit  = h;
    *miss = (a >= h) ? (a - h) : 0u;
}

/* ---- clock -------------------------------------------------------------
 * Start the crystal, point clk_ref at it, point clk_sys at clk_ref, and give
 * clk_peri the same clock so the UART divisors below mean what they say.
 * No PLL: see note 2 at the top of this file.
 *
 * EVERY MUX HERE IS GLITCHLESS AND HAS A SELECTED REGISTER, and each switch
 * is waited for. The reset state of this tree is clk_ref on the ring
 * oscillator, whose frequency is not specified tightly enough for a UART, so
 * the wait is what makes the console work rather than a nicety.
 *
 * THE STARTUP DELAY IS IN UNITS OF 256 CRYSTAL CYCLES: 47 is the SDK's
 * ((12000 + 128) / 256), about 1 ms at 12 MHz, which is the delay Raspberry
 * Pi's own board support uses for the crystals these boards fit. */
static void clock_init(void)
{
    XOSC_STARTUP = 47u;
    XOSC_CTRL = XOSC_CTRL_FREQ_1_15MHZ | XOSC_CTRL_ENABLE;
    while (!(XOSC_STATUS & XOSC_STATUS_STABLE)) {}

    CLK_REF_DIV = CLK_DIV_1;
    CLK_REF_CTRL = CLK_REF_SRC_XOSC;
    while (!(CLK_REF_SELECTED & (1u << CLK_REF_SRC_XOSC))) {}

    CLK_SYS_DIV = CLK_DIV_1;
    CLK_SYS_CTRL = CLK_SYS_SRC_CLK_REF;
    while (!(CLK_SYS_SELECTED & (1u << CLK_SYS_SRC_CLK_REF))) {}

    /* clk_peri has no glitchless mux and no SELECTED to wait on, so it is
     * disabled, pointed at clk_sys, then enabled. It comes out of reset
     * disabled, so this is the first thing that has ever driven it. */
    CLK_PERI_CTRL = 0u;
    CLK_PERI_CTRL = (CLK_PERI_AUXSRC_CLK_SYS << 5);
    CLK_PERI_CTRL = (CLK_PERI_AUXSRC_CLK_SYS << 5) | CLK_PERI_CTRL_ENABLE;
}

/* Everything on this part comes out of chip reset HELD IN RESET, and a
 * register in a block still held reads back zero instead of faulting. So a
 * UART configured without this would take every write silently and send
 * nothing, which from the host is a dead victim rather than an error. */
static void unreset(uint32_t bits)
{
    RESETS_RESET &= ~bits;
    while ((RESETS_RESET_DONE & bits) != bits) {}
}

/* ---- the link ----------------------------------------------------------
 * An ARM PL011, and the divisor is computed rather than looked up: the
 * fractional part makes a 6.51-cycle divisor at 12 MHz land within 0.1 % of
 * 115200, which no integer divisor would.
 *
 * WRITING LCR_H IS WHAT LATCHES IBRD AND FBRD. That is a PL011 property and
 * not a line to reorder: the two divisor registers are held in a shadow
 * until LCR_H is written, so setting them after it programs the old rate and
 * the host sees framing errors. */
static void uart_init(void)
{
    uint32_t div = (8u * SS_CLK_HZ + SS_BAUD / 2u) / SS_BAUD;

    unreset(RESETS_UART0);

    UART_CR = 0u;
    UART_IBRD = div >> 7;
    UART_FBRD = ((div & 0x7Fu) + 1u) >> 1;
    UART_LCR_H = UART_LCR_H_WLEN8 | UART_LCR_H_FEN;
    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;

    /* The pads are already in a sane state out of reset on this part -
     * input enabled, pull-down, 4 mA - so only the function select is
     * touched. OD is cleared on the TX pad in case something before this
     * left it set. */
    PAD_CTRL(UART_TX_PIN) = (PAD_CTRL(UART_TX_PIN) & ~(uint32_t)PAD_OD) | PAD_IE;
    PAD_CTRL(UART_RX_PIN) = (PAD_CTRL(UART_RX_PIN) & ~(uint32_t)PAD_OD) | PAD_IE;
    IO_CTRL(UART_TX_PIN) = IO_FUNC_UART;
    IO_CTRL(UART_RX_PIN) = IO_FUNC_UART;
}

void ssb_put(uint8_t c)
{
    while (UART_FR & UART_FR_TXFF) {}
    UART_DR = (uint32_t)c;
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
    while (UART_FR & UART_FR_RXFE) {}
    return (int)(UART_DR & 0xFFu);
}

/* ---- trigger ----------------------------------------------------------- */
void ssb_trig_high(void) { SIO_GPIO_OUT_SET = (1u << TRIG_PIN); }
void ssb_trig_low(void)  { SIO_GPIO_OUT_CLR = (1u << TRIG_PIN); }

static void trig_init(void)
{
    SIO_GPIO_OUT_CLR = (1u << TRIG_PIN);               /* start low */
    SIO_GPIO_OE_SET  = (1u << TRIG_PIN);
    PAD_CTRL(TRIG_PIN) = PAD_CTRL(TRIG_PIN) & ~(uint32_t)PAD_OD;
    IO_CTRL(TRIG_PIN) = IO_FUNC_SIO;
}

/* ---- the cycle counter -------------------------------------------------
 * SYSTICK IS THE ONLY TIME BASE ON THIS PART, and the DWT arm below exists
 * to say so from the device rather than to be used.
 *
 * ARMv6-M HAS NO DWT CYCLE COUNTER. The ROM table advertises a DWT at
 * 0xE0001000 and a probe duly reports watchpoint units, which reads as
 * "there is a counter here". There is not: CYCCNT is a v7-M feature. The
 * registers are readable and CYCCNT reads a constant, so every interval
 * derived from it comes out exactly zero - a constant-time result, not a
 * missing counter, which is the F429's FLASH_ACR-reads-zero trap in another
 * costume. So the DWT arm WRITES the enables, does work, and reports through
 * ssb_has_cyc whether the counter moved. On this part that check is expected
 * to FAIL and the build to report no counter, and that is the contract
 * working: SS_V_ENC_CYC then answers with an error and the host learns it
 * from the device. Build with TIMING=systick, which is the default. */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                 /* would count UP over 32 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    DEMCR |= DEMCR_TRCENA;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    __asm volatile("dsb; isb");
    a = DWT_CYCCNT;
    for (volatile int i = 0; i < 64; i++) { }
    b = DWT_CYCCNT;
    g_cyc_ok = (a != b);
}

#elif defined(SS_TIMING_SYSTICK)
int      ssb_has_cyc(void)  { return 1; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    /* THE BOARD OWNS THE SUBTRACTION, and the direction is why the hook
     * exists. SysTick counts DOWN, so the later read is the smaller one and
     * the difference is first - second; the mask is 24 bits because that is
     * the whole counter, and it is what makes one wrap come out right
     * instead of as a huge positive number. At 12 MHz one wrap is 1.4 s,
     * which no measured region comes near. */
    return (first - second) & 0xFFFFFFu;
}

static void cyc_init(void)
{
    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_CLKSOURCE;   /* clk_sys, no IRQ */
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
    unreset(RESETS_IO_BANK0 | RESETS_PADS_BANK0);
    clock_init();
    uart_init();
    trig_init();
    cyc_init();

    /* Leave the accelerator where a reset-time write would leave it: the
     * cache enabled, and cold. XIP_CTRL.EN reads 1 after reset and boot2
     * does not change it, so this is what the part ships as - with the tag
     * memory flushed, which boot2 running through the cache has already
     * dirtied. The witness bits are read-only and are not passed. */
    ssb_acr_set(RP_ACR_CACHE_EN);
}

/* CORE 1 IS NEVER STARTED AND NOTHING HERE STARTS IT, which is worth a line
 * because the two cores share the one cache. The bootrom leaves core 1 in a
 * wait loop with no XIP access of its own, and anything that woke it would be
 * an uncontrolled evictor of the exact 16 KB under test. There is no code in
 * this port that writes the SIO FIFO or the PSM, which is the only way it
 * could be launched. */
