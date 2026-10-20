/* TI CC1352R1F3 (LAUNCHXL-CC1352R1) board layer.
 *
 * THIS PORT HAS NEVER BEEN RUN ON SILICON. It compiles, and every register
 * address and bit position in it was copied from the rig that did run on
 * this part (rigs/fw_cc1352_aes/ in the research tree); nothing in it has
 * been seen to produce a byte on a wire. Treat every behavioural claim
 * below as inherited from that rig, not as verified here. Checklist steps
 * 4 to 7 of docs/PORTING.md are all outstanding.
 *
 * This part needs the packed accelerator word for a different reason than the
 * H723 did. Its
 * accelerator is the VIMS: one 8 KB array that VIMS_CTL.MODE puts into
 * CACHE, into GPRAM (addressable RAM at 0x11000000) or OFF, and BEHIND
 * that array a one-entry 16-byte flash line buffer with its own documented
 * disable bit. mcufw/cc1352r1/regs.h holds the packing.
 *
 * TWO THINGS THE RESEARCH TREE MEASURED HERE THAT SHAPE THIS FILE.
 *
 * Turning the accelerator off does not close the channel - it makes the
 * byte S-box leak harder, sigma 1.539 in CACHE against 4.289 in OFF,
 * because with only a one-entry row register adjacency decides everything.
 * What closes it is IDCODE_LB_DIS = 1 together with MODE off or gpram,
 * which is bit-exactly constant. So the interesting words for this part
 * are not ordered "more acceleration to less", and the board must be able
 * to set the mode and the line-buffer disable independently.
 *
 * A VIMS mode change is not instant, and it is not one write. The rig
 * waits for VIMS_STAT.MODE_CHANGING to clear, forces a GPRAM<->CACHE
 * transition through OFF rather than going direct, and then waits for
 * VIMS_STAT.INV, which covers the automatic invalidate. Skipping the
 * second wait is this part's version of the ST ICRST/DCRST trap: stale
 * lines survive the reconfigure and the next measurement is of something
 * else. vims_apply() below reproduces all three waits.
 *
 * ONE DELIBERATE DEVIATION FROM THE RIG, AND IT IS NOT SETTLED. The rig
 * puts vims_apply() in a .ramfunc section and runs it from SRAM, because
 * the Icode path being reconfigured is the one fetching those very
 * instructions. This tree's section layout is shared by every part
 * (instrumentation/victim/ld/sections.ld) and has no output section for
 * code in RAM, and a port may not edit it, so here the same sequence runs
 * from flash. WHETHER THAT WORKS IS NOT ESTABLISHED. Nothing in this
 * tree or in the research tree measured what happens when the sequence
 * is fetched through the VIMS it is reconfiguring; the rig avoided the
 * question rather than answering it, and this port cannot avoid it. If a
 * mode change ever hangs this board, that is the first thing to look at,
 * and
 * the fix is a RAM-code section in the shared script rather than anything
 * in this directory.
 *
 * The clock is the reset state: CPUCLKDIV = /1, which the research tree
 * calls 48 MHz. Note what that figure is: the part's rated maximum with
 * the divider at /1, NOT a frequency measured against a reference. The
 * attempt to measure it on chip returned zero because AON_RTC was never
 * started. Cycle COUNTS are exact regardless, and nothing in this victim
 * depends on the absolute number except the UART divisor, which is
 * derived from the UART's own 48 MHz clock rather than from the CPU
 * clock.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif

/* The UART's clock, which is NOT the CPU clock and is not divided by
 * PRCM_CPUCLKDIV. The rig changes CPUCLKDIV at run time and goes on
 * answering on the same UART with the same divisor, which is where this
 * comes from. */
#define UARTCLK 48000000u

/* The CPU clock at CPUCLKDIV = /1. See the file header for what this
 * number is and is not. */
#define CPU_HZ_DIV1 48000000u

#if !defined(SS_LINK_VCP)
#  error "cc1352r1 offers LINK=vcp only - see mcufw/cc1352r1/board.mk"
#endif

/* The XDS110's Application UART, through the isolation jumpers: target TX
 * on DIO13 and target RX on DIO12. Measured on the board, by a firmware
 * that swept candidate pairs until the host answered, and then confirmed
 * against the COM port. They are fixed here rather than swept: the sweep
 * on that board latched tx=13 rx=3 with the host silent, because DIO3
 * picks up a transition from the sweep's own TX activity, and the result
 * was a board that printed a correct banner and answered nothing. */
#define UART_TX_DIO 13u
#define UART_RX_DIO 12u

const char *ssb_name(void) { return "cc1352r1"; }

static void clk_load(void)
{
    PRCM_CLKLOADCTL = PRCM_CLKLOADCTL_LOAD;
    while (!(PRCM_CLKLOADCTL & PRCM_CLKLOADCTL_LOAD_DONE)) {}
}

/* ---- the accelerator --------------------------------------------------
 * TRAP 1 on this part is not a wait state. There is no reachable
 * wait-state axis here at all, so nothing this function can be asked for
 * will under-program the flash for the clock. What it refuses instead is
 * a word that does not describe a configuration: MODE = 2, which this
 * part does not have, and any bit outside the packing. Both would
 * otherwise be written straight into VIMS_CTL, and a reserved bit that
 * sticks is recorded as a configuration that was never tried.
 *
 * TRAP 2 is the reconfigure dance, and on this part it is the three waits
 * plus the detour through OFF rather than a cache-reset pulse.
 */
static int acr_legal(uint32_t v)
{
    if (v & ~CC_ACR_VALID_Msk) return 0;
    if ((v & CC_ACR_MODE_Msk) == 2u) return 0;   /* no such mode */
    return 1;
}

static void vims_apply(uint32_t ctl)
{
    uint32_t cur = VIMS_CTL;
    uint32_t curmode = cur & VIMS_MODE_M;
    uint32_t newmode = ctl & VIMS_MODE_M;

    while (VIMS_STAT & VIMS_STAT_MODE_CHANGING) {}

    /* GPRAM and CACHE are the same physical array doing two different
     * jobs, and the transition between them goes through OFF. TI's
     * driverlib insists on that, so the rig does it explicitly rather
     * than hoping the hardware sequences it. */
    if (curmode != newmode
        && curmode != VIMS_MODE_OFF && newmode != VIMS_MODE_OFF) {
        VIMS_CTL = (cur & ~VIMS_MODE_M) | VIMS_MODE_OFF;
        __asm volatile("dsb 0xF" ::: "memory");
        while (VIMS_STAT & VIMS_STAT_MODE_CHANGING) {}
    }

    VIMS_CTL = ctl;
    __asm volatile("dsb 0xF" ::: "memory");
    while (VIMS_STAT & VIMS_STAT_MODE_CHANGING) {}
    while (VIMS_STAT & VIMS_STAT_INV) {}
    __asm volatile("isb 0xF" ::: "memory");
}

void ssb_acr_set(uint32_t v)
{
    uint32_t ctl;

    if (!acr_legal(v)) return;

    /* The word carries MODE, PREF_EN and the two line-buffer disables.
     * Everything else in VIMS_CTL is carried over from the hardware
     * rather than chosen here: ARB_CFG because one master makes it
     * meaningless, DYN_CG_EN because the rig preserves it. */
    ctl = (v & CC_ACR_VALID_Msk)
        | (VIMS_CTL & (VIMS_ARB_CFG | VIMS_DYN_CG_EN));

    vims_apply(ctl);
}

uint32_t ssb_acr_get(void)
{
    /* READ the register and mask it down to the word. Never rebuild it
     * from what was requested: a refused configuration has to come back
     * as the one that is actually running. */
    return VIMS_CTL & CC_ACR_VALID_Msk;
}

/* ---- the core clock ---------------------------------------------------
 * PRCM_CPUCLKDIV.RATIO is the gain knob on this part, in place of the
 * wait-state field other parts here sweep. TI marks the register internal
 * and the research tree measured it: /2 makes CACHE mode bit-exactly
 * constant and halves the amplitude in OFF mode, reversibly.
 *
 * Reported from the register rather than from a constant, so a capture
 * records the divider that was actually in force.
 */
uint32_t ssb_clk_hz(void)
{
    return (PRCM_CPUCLKDIV & PRCM_CPUCLKDIV_RATIO)
         ? (CPU_HZ_DIV1 / 2u) : CPU_HZ_DIV1;
}

int ssb_set_mhz(uint32_t mhz)
{
    if (mhz == 48u)      PRCM_CPUCLKDIV = 0u;
    else if (mhz == 24u) PRCM_CPUCLKDIV = PRCM_CPUCLKDIV_RATIO;
    else                 return 0;               /* refuse, state unchanged */

    clk_load();
    __asm volatile("dsb 0xF; isb 0xF" ::: "memory");
    return 1;
}

int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- the link ---------------------------------------------------------
 * Power domain, then clock gate, then the load that makes the gate take
 * effect, then the pin mux. Each step is in that order in the rig and the
 * UART registers read as zero until the first two have happened.
 */
static void uart_init(void)
{
    uint32_t div;

    PRCM_PDCTL0 |= PRCM_PD_SERIAL | PRCM_PD_PERIPH;
    while ((PRCM_PDSTAT0 & (PRCM_PD_SERIAL | PRCM_PD_PERIPH))
           != (PRCM_PD_SERIAL | PRCM_PD_PERIPH)) {}
    PRCM_GPIOCLKGR |= 1u;
    PRCM_UARTCLKGR |= 1u;
    clk_load();

    UART_CTL = 0u;
    /* 48 MHz / (16 * baud), with the fraction in Q6. */
    div = (UARTCLK * 4u) / SS_BAUD;
    UART_IBRD = div / 64u;
    UART_FBRD = div % 64u;
    UART_LCRH = UART_LCRH_WLEN8 | UART_LCRH_FEN;
    UART_CTL  = UART_CTL_UARTEN | UART_CTL_TXE | UART_CTL_RXE;

    IOCFG(UART_TX_DIO) = IOC_PORT_UART0_TX | IOC_PULL_DIS;
    IOCFG(UART_RX_DIO) = IOC_PORT_UART0_RX | IOC_PULL_DIS | IOC_IE;
}

void ssb_put(uint8_t c)
{
    while (UART_FR & UART_FR_TXFF) {}
    UART_DR = c;
}

int ssb_get(void)
{
#if defined(SS_TIMING_DWT)
    /* Re-armed here rather than at boot: a debug probe clears TRCENA when
     * it DISCONNECTS, which is after the liveness check has passed, and
     * the device then reports a counter it no longer has. The console
     * read is outside every measured region. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb 0xF; isb 0xF" ::: "memory");
    }
#endif
    while (UART_FR & UART_FR_RXFE) {}
    return (int)(UART_DR & 0xFFu);
}

/* ---- the trigger marker -----------------------------------------------
 * THERE IS NO TRIGGER PIN ON THIS PORT, and these are honest no-ops
 * rather than writes to a plausible register. Driving a DIO as an output
 * on this part needs the GPIO peripheral's DOUT/DOE registers as well as
 * the IOC mux, and the rig never drove a pin - it timed everything on
 * chip with DWT_CYCCNT - so this tree has no address for them that was
 * read off anything. Inventing one would write into whatever is at that
 * address, which on this part reads back plausibly rather than faulting.
 *
 * The consequence: an externally instrumented arm (scope, SS_V_TRIG_CTL)
 * marks nothing here. The on-chip counter is the only usable timebase on
 * this board until the GPIO addresses are established from TI's target
 * database the way everything in regs.h was.
 */
void ssb_trig_high(void) { }
void ssb_trig_low(void)  { }

/* ---- the cycle counter ------------------------------------------------
 * TRAP 3: the board owns the subtraction. DWT counts UP over 32 bits,
 * SysTick counts DOWN over 24.
 */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                       /* counts UP, 32 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0u;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    __asm volatile("dsb 0xF; isb 0xF" ::: "memory");

    /* Report what is true, and in two ways: the part may say it has no
     * cycle counter, and a counter that says it is enabled may still not
     * be moving. The rig reads NOCYCCNT as 0 on this die. */
    if (DWT_CTRL & DWT_CTRL_NOCYCCNT) { g_cyc_ok = 0; return; }
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
    return (first - second) & 0xFFFFFFu;         /* counts DOWN, 24 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0u;
    SYST_CSR = 5u;                               /* ENABLE | core clock */

    /* Checked rather than assumed. The rig timed this part with DWT and
     * never used SysTick here, so this arm has no evidence behind it on
     * this silicon; a counter that is not moving must report 0 instead of
     * handing back a column of equal numbers that reads as constant
     * time. */
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

void ssb_irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
void ssb_irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

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
    /* The boot state of this part is VIMS_CTL = 0, read off the die with
     * the core halted: MODE = GPRAM, prefetch off, both line buffers
     * ENABLED, which is a leaking configuration. TI's driverlib startup
     * would move it to CACHE, which is also a leaking configuration. This
     * victim does not run driverlib, so it picks CACHE deliberately,
     * because that is what firmware built on TI's SDK runs and it is the
     * cell the research tree's numbers are anchored to. The host can set
     * any word in the packing afterwards, through the same code path. */
    ssb_acr_set(CC_ACR_MODE_CACHE);
    uart_init();
    cyc_init();
}
