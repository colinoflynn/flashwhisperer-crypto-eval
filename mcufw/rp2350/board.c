/* Raspberry Pi Pico 2 (RP2350) board layer.
 *
 * THIS PORT IS UNTESTED ON SILICON. It was written against the register
 * definitions in the research tree's RP2350 rig (rigs/fw_rp2350_aes/main.c,
 * which HAS been run on this part) and the pico-sdk 2.x register headers read
 * for their numbers, and it has been compiled and nothing more. No board was
 * attached, nothing was flashed, and no byte has been off a wire. Steps 4
 * through 7 of docs/PORTING.md's checklist - IDENT agreeing with
 * `make addr`, ciphertexts matching the reference AES, a bit-exact RAM twin,
 * and a recovery - have not been run. Treat every number this file could
 * produce as unconfirmed until they have.
 *
 * WHY THIS PART IS WORTH HAVING, AND IT IS A DIFFERENT REASON FROM THE
 * RP2040'S. The research tree's standard build on this part performs NO
 * FLASH ACCESS AT ALL during the S-box encryption, which is why it is
 * recorded as not broken rather than as safe: the question was never put to
 * it. A build that places the table in the XIP window, proves the address is
 * in that window, and runs the recovery is what puts it. That is this port's
 * whole job, and it is why mcufw/rp2350/memory.ld is the longest comment
 * here.
 *
 * IT IS ALSO THE ONLY PART ON THIS BENCH WHOSE FLASH TIMING IS A LIVE
 * REGISTER. QMI_M0_TIMING.CLKDIV is the flash SCLK divider and the silicon
 * permits writing it while the QMI is serving that window - so the thing the
 * RP2040 can only vary by relinking a boot2 is one word here, and the
 * ledger's claim that wait states are a gain factor rather than information
 * is directly testable. It is in the accelerator word for that reason. The
 * packing, and the five fields deliberately left out of it, are written up at
 * the top of regs.h, which also lists the five places where this part is not
 * an RP2040 with a bigger number.
 *
 * THREE THINGS IN HERE ARE NOT WHAT AN ST BOARD FILE WOULD DO.
 *
 *   1. ssb_acr_set IS RAM-RESIDENT. It writes XIP_CTRL, writes
 *      QMI_M0_TIMING, and sweeps 2048 byte-writes through the cache
 *      maintenance window - all operations on the path the core is fetching
 *      instructions through. On the RP2040 the research tree measured that
 *      writing that register file from XIP-resident code lands in a
 *      HardFault that faulted again; here the SDK makes every maintenance
 *      helper RAM-resident and the rig follows it. Nothing has been run on
 *      this port, so it takes the cautious side.
 *
 *   2. THE CLOCK STAYS AT THE CRYSTAL, 12 MHz, AND NO PLL IS PROGRAMMED.
 *      The flash clock is clk_sys divided by CLKDIV, so the cost of a miss
 *      IN CORE CYCLES is a divider ratio and does not depend on clk_sys at
 *      all. Raising clk_sys would multiply both sides and change nothing
 *      this project measures, at the price of a PLL bring-up order whose
 *      failure mode is a part that stops with no output. It also means the
 *      flash SCLK cannot be pushed out of spec by a CLKDIV sweep: at
 *      12 MHz, CLKDIV 2 is 6 MHz.
 *
 *      THE ONE THING THAT ORDER BUYS, and it is worth naming because the
 *      SDK's register documentation warns about the other direction: a
 *      CLKDIV increase must be in effect before clk_sys rises. Never raising
 *      clk_sys means that ordering constraint cannot be violated here.
 *
 *   3. ssb_set_mhz RETURNS 0. With no PLL there is one core rate.
 *
 * CORE 1 IS NEVER STARTED, and nothing here starts it, which matters because
 * both cores share the one cache. The bootrom leaves it parked. There is no
 * code in this port that writes the SIO FIFO or the PSM.
 *
 * THE RISC-V HALF OF THIS PART IS NOT BUILT AND CANNOT BE FROM HERE. The
 * image is ARM Secure, which is what the IMAGE_DEF block says; a Hazard3
 * image would need a different core, a different toolchain and an
 * ENTRY_POINT item in that block.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

/* 12 MHz: the crystal on a Pico 2, and the only rate this port runs. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 12u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

#if !defined(SS_LINK_VCP)
#  error "rp2350 offers LINK=vcp only - see mcufw/rp2350/board.mk"
#endif

/* The console pins. GP0 is UART0 TX and GP1 is UART0 RX on function 2, the
 * same as on the RP2040 and the same as PICO_DEFAULT_UART_TX_PIN/RX_PIN for
 * PICO_BOARD=pico2. A Pico 2 has no on-board bridge to the host for these,
 * so `vcp` here means the probe's CDC or an adapter on the header. */
#define UART_TX_PIN 0u
#define UART_RX_PIN 1u

/* The trigger pin, and THE ONE THING IN THIS FILE TAKEN FROM A BOARD
 * DOCUMENT RATHER THAN FROM A RIG. The RP2350 rig drives no trigger - it
 * times itself - so the project has not established a pin for this part.
 * GP25 is the on-board LED on a Pico 2, a plain SIO pin with nothing else on
 * it. If the probe ends up elsewhere this is the line to change and the only
 * one. */
#define TRIG_PIN 25u

const char *ssb_name(void) { return "rp2350"; }

/* ---- the accelerator word ---------------------------------------------
 * RAM-RESIDENT, see note 1 at the top. `noinline` as well as the section
 * attribute, because the rig records that a static function called once gets
 * inlined into its caller at -O2 and then runs from flash anyway, silently.
 *
 * TRAP 1, refuse what cannot be run, and ON THIS PART THERE IS SOMETHING
 * REAL TO REFUSE - which there was not on the RP2040 and is not on the
 * nRF52840. Two things:
 *
 *   A word carrying bits this port does not implement. POWER_DOWN above
 *   all: it powers down the cache SRAM the core is fetching through.
 *
 *   CLKDIV below 2. Divisor 1 is legal in the register and is the one
 *   setting that can stop the flash answering at all, at which point the
 *   next instruction fetch has nowhere to come from and the part needs a
 *   BOOTSEL cycle to recover. The research tree's rig CLAMPS to 2 and this
 *   port REFUSES instead, deliberately: the contract makes a refusal
 *   visible, because SS_V_SET_ACR replies with the readback, so a sweep
 *   learns the word did not take rather than filing a capture under a
 *   divisor that was quietly changed under it.
 *
 * TRAP 2, the reset-equivalent dance, and it is a longer dance here than
 * anywhere else in the tree because THERE IS NO FLUSH REGISTER. Invalidating
 * is 2048 byte-writes into the maintenance window with the operation in the
 * low address bits. The order below is:
 *
 *   drop both enables AND SPLIT_WAYS       - SPLIT first, so the sweep can
 *                                            reach both ways; the SDK's own
 *                                            note says the invalidate must
 *                                            happen with SPLIT_WAYS clear
 *   invalidate every line by set/way       - the stronger form of "reset the
 *                                            caches while they are disabled"
 *   write CLKDIV, then a dummy access      - the SDK asks for an access plus
 *                                            barriers so the new divisor is
 *                                            in effect before anything
 *                                            depends on it
 *   write the wanted enables and SPLIT     - into a cache that is empty
 *
 * The sweep is a bounded loop over a known geometry, not a poll of a status
 * bit, so it cannot spin forever - which is the failure that presents as a
 * board that never says hello. */
__attribute__((section(".ramfunc"), noinline, used))
void ssb_acr_set(uint32_t v)
{
    uint32_t div, xip, base, off;

    if (v & ~(uint32_t)RP_ACR_MASK) return;

    div = v & RP_ACR_CLKDIV_Msk;
    if (div < QMI_CLKDIV_MIN) return;

    xip = (v >> RP_ACR_XIP_Pos) & RP_XIP_ALLOWED;

    __asm volatile("cpsid i" ::: "memory");

    base = XIP_CTRL_REG & ~(uint32_t)RP_XIP_ALLOWED;
    XIP_CTRL_REG = base;
    __asm volatile("dsb; isb");

    for (off = XIP_WINDOW_BYTES - XIP_CACHE_BYTES;
         off < XIP_WINDOW_BYTES;
         off += XIP_CACHE_LINE) {
        *(volatile uint8_t *)(uintptr_t)
            (XIP_MAINT_ADDR + off + XIP_MAINT_INVAL_SETWAY) = 0u;
    }
    __asm volatile("dsb; isb");

    QMI_M0_TIMING = (QMI_M0_TIMING & ~(uint32_t)QMI_M0_TIMING_CLKDIV_Msk) | div;
    (void)QMI_M0_TIMING;
    __asm volatile("dsb; isb");
    (void)*(volatile const uint32_t *)(uintptr_t)XIP_BASE_ADDR;
    __asm volatile("dsb; isb");

    XIP_CTRL_REG = base | xip;
    __asm volatile("dsb; isb");
    (void)XIP_CTRL_REG;

    __asm volatile("cpsie i" ::: "memory");
}

/* Read, never reconstructed from the request - which is what makes a refused
 * configuration visible instead of silently recorded under the name it was
 * asked for. Both halves are register reads. */
uint32_t ssb_acr_get(void)
{
    return (QMI_M0_TIMING & QMI_M0_TIMING_CLKDIV_Msk)
         | ((XIP_CTRL_REG & RP_XIP_ALLOWED) << RP_ACR_XIP_Pos);
}

uint32_t ssb_clk_hz(void)  { return SS_CLK_HZ; }
int      ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* ---- the hit/miss counters ---------------------------------------------
 * XIP_CTR_HIT counts XIP accesses that hit and XIP_CTR_ACC counts every XIP
 * access, both cleared by writing any value.
 *
 * THE CONTRACT ASKS FOR hit AND miss AND THIS PART COUNTS hit AND total, so
 * the miss figure is a subtraction done here rather than a register. It is
 * exact - every access either hits or misses - and a saturated ACC is
 * reported as a zero miss count rather than as a wrapped difference.
 *
 * THE CACHE IS UNIFIED, so ACC counts instruction fetches as well as table
 * reads, and a victim running out of XIP counts mostly its own code. These
 * are ground truth about the cache, not about the table; separating the two
 * is what the research tree's RAM-resident probes are for. Being able to
 * check timing against the hardware's own account of what the cache did is
 * something only the two RP parts here can do at all. */
int ssb_has_monitors(void) { return 1; }

void ssb_monitors_reset(void)
{
    XIP_CTR_HIT_REG = 1u;                  /* any value clears */
    XIP_CTR_ACC_REG = 1u;
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
 * THE DIVISOR CONSTANT IS NOT THE RP2040'S. An RP2350 clock divisor is INT
 * in bits 31:16; the RP2040's 1 << 8 would program a fractional divider
 * here. Both are in their own regs.h as CLK_DIV_1 and they are different
 * numbers.
 *
 * clk_sys is written explicitly rather than inherited: the reset state has it
 * on clk_ref, but this part's bootrom does more before handing over than the
 * RP2040's does and being explicit costs two register writes. */
static void clock_init(void)
{
    XOSC_STARTUP = 47u;                    /* ~1 ms in units of 256 cycles */
    XOSC_CTRL = XOSC_CTRL_FREQ_1_15MHZ | XOSC_CTRL_ENABLE;
    while (!(XOSC_STATUS & XOSC_STATUS_STABLE)) {}

    CLK_REF_DIV = CLK_DIV_1;
    CLK_REF_CTRL = CLK_REF_SRC_XOSC;
    while (!(CLK_REF_SELECTED & (1u << CLK_REF_SRC_XOSC))) {}

    CLK_SYS_DIV = CLK_DIV_1;
    CLK_SYS_CTRL = CLK_SYS_SRC_CLK_REF;
    while (!(CLK_SYS_SELECTED & (1u << CLK_SYS_SRC_CLK_REF))) {}

    /* clk_peri has no glitchless mux and no SELECTED to wait on, so it is
     * disabled, pointed at clk_sys, then enabled. It has a divider on this
     * part, which the RP2040 does not, and it is set to one. */
    CLK_PERI_CTRL = 0u;
    CLK_PERI_DIV = CLK_DIV_1;
    CLK_PERI_CTRL = (CLK_PERI_AUXSRC_CLK_SYS << 5);
    CLK_PERI_CTRL = (CLK_PERI_AUXSRC_CLK_SYS << 5) | CLK_PERI_CTRL_ENABLE;
}

/* Peripherals come out of chip reset held in reset, and a register in a block
 * still held reads back zero instead of faulting. A UART configured without
 * this takes every write silently and sends nothing, which from the host is a
 * dead victim rather than an error. */
static void unreset(uint32_t bits)
{
    RESETS_RESET &= ~bits;
    while ((RESETS_RESET_DONE & bits) != bits) {}
}

/* Give a pad to a peripheral or to SIO. THE ISO BIT IS THE WHOLE REASON THIS
 * IS A FUNCTION. Every pad on this part resets with ISO set and does nothing
 * at all until it is cleared - the RP2040 has no such bit, so an RP2040 UART
 * bring-up copied across configures everything correctly and sends nothing.
 * It is cleared LAST, after the function select, so the pad is never driven
 * while it is still being configured. */
static void pad_claim(uint32_t pin, uint32_t func, uint32_t pad_bits)
{
    PAD_CTRL(pin) = (PAD_CTRL(pin) & ~(uint32_t)PAD_OD) | pad_bits | PAD_ISO;
    IO_CTRL(pin) = func;
    PAD_CTRL(pin) = PAD_CTRL(pin) & ~(uint32_t)PAD_ISO;
}

/* ---- the link ----------------------------------------------------------
 * An ARM PL011 at 0x40070000, not the RP2040's 0x40034000. The divisor is
 * computed rather than looked up, and WRITING LCR_H IS WHAT LATCHES IBRD AND
 * FBRD - a PL011 property and not a line to reorder, because the two divisor
 * registers are held in a shadow until LCR_H is written. */
static void uart_init(void)
{
    uint32_t div = (8u * SS_CLK_HZ + SS_BAUD / 2u) / SS_BAUD;

    unreset(RESETS_UART0);

    UART_CR = 0u;
    UART_IBRD = div >> 7;
    UART_FBRD = ((div & 0x7Fu) + 1u) >> 1;
    UART_LCR_H = UART_LCR_H_WLEN8 | UART_LCR_H_FEN;
    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;

    pad_claim(UART_TX_PIN, IO_FUNC_UART, PAD_IE);
    pad_claim(UART_RX_PIN, IO_FUNC_UART, PAD_IE);
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
        DWT_LAR = DWT_LAR_KEY;
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
    pad_claim(TRIG_PIN, IO_FUNC_SIO, 0u);
}

/* ---- the cycle counter -------------------------------------------------
 * THIS PART HAS A REAL ONE, which is the asymmetry with the RP2040 beside
 * it worth stating out loud: there the expected answer to "is CYCCNT
 * implemented?" is no, because ARMv6-M has no such counter; here the core is
 * a Cortex-M33 and the answer is yes, 32 bits, counting up. Both are
 * CHECKED rather than assumed, which is what _PROCEDURE.md's "check the
 * counter is running" means even for a counter that is architecturally
 * present - the H723 and the F429 both taught this project that a register
 * reading a plausible number is not the same as a register working.
 *
 * SysTick remains the default because build.mk's default is systick and
 * because it needs no debug power domain and no software lock. TIMING=dwt is
 * the better instrument on this part if the liveness check passes. */
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
    /* ARMv8-M puts a software lock in front of the DWT. Writing the key is
     * harmless where there is no lock and necessary where there is, and
     * without it the enable below is silently dropped - which presents as a
     * counter that reads a constant, i.e. as a constant-time result. */
    DWT_LAR = DWT_LAR_KEY;
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
     * instead of as a huge positive number. At 12 MHz one wrap is 1.4 s. */
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

    /* Leave the accelerator where a reset-time write would leave it: both
     * cache enables set, the ways unsplit, and CLKDIV at its reset value of
     * 4 - with the cache invalidated, which the bootrom's own XIP setup has
     * already dirtied. */
    ssb_acr_set(((XIP_CTRL_EN_SECURE | XIP_CTRL_EN_NONSECURE) << RP_ACR_XIP_Pos)
                | QMI_CLKDIV_RESET);
}
