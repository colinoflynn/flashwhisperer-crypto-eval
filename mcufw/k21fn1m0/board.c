/* NXP Kinetis MK21FN1M0VMC12 board layer (TWR-K21F120M).
 *
 * THIS PORT IS UNTESTED ON SILICON. It compiles and it links; no board has
 * been attached, nothing here has been flashed, and no measurement has been
 * taken with it. Every register address and bit position was copied from
 * rigs/fw_k21_aes/ in the flash-accelerator-tests tree, which IS the
 * version that has run on this part, and the places where that rig does not
 * answer the question are marked "not established" rather than filled in.
 *
 * The first NXP Kinetis in this tree. What is different about it:
 *
 *   * The accelerator is not one register and not one bank. Two FMC
 *     control registers, five separately switchable enables each, a
 *     replacement policy and a way lock, over two independent flash blocks.
 *     regs.h packs the lot into the single word the contract asks for and
 *     documents the packing at the top.
 *
 *   * The wait state cannot be written. B0RWSC is read-only and the FMC
 *     derives it from the system:flash clock ratio, so the wait-state axis
 *     is reached by dividing the flash clock down through SIM_CLKDIV1 and
 *     reading B0RWSC back to see what the FMC decided. That readback is a
 *     confirmation the ST parts here cannot give, and it is in the word.
 *
 *   * THE BOARD HAS NO USABLE SERIAL PORT, which is why the console below
 *     is not a UART. See the console section.
 *
 * Clock is the reset state: FLL engaged internal, which the rig calls
 * "~21 MHz" and "the reset DCO (~21.1 MHz)" and never pins to a number.
 * This port does not move it. The rig can walk FEI to PEE and run the part
 * at its rated 120 MHz, and that is a different point on the axis rather
 * than a scaled version of this one, because the wait state is derived from
 * the clock ratio - but bringing the PLL up needs a crystal RANGE and load
 * capacitance that are board properties this port has no measurement for,
 * so it is left out and ssb_set_mhz() reports that there is no choice here.
 */
#include <stdint.h>
#include "ss_board.h"
#include "regs.h"

/* The reset FLL-engaged-internal clock. THIS IS A NOMINAL FIGURE, NOT A
 * MEASUREMENT: rigs/fw_k21_aes/main.c describes the reset clock as "~21 MHz"
 * and as "the reset DCO (~21.1 MHz)" and nothing on this bench has pinned it
 * closer. It is reported by SS_V_IDENT and it is the divisor in the flash
 * clock check below, which is the only arithmetic in this file that depends
 * on it; nothing here derives a baud rate from it. Treat a cycle count as
 * cycles, not as time, until somebody measures the DCO. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 21u
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

/* rigs/fw_k21_aes/main.c, cmd 10: "the flash clock must be <= 25 MHz AND an
 * integer divide of the core clock". OUTDIV4 makes the divide integral by
 * construction, so the ceiling is the half that has to be checked. */
#define K21_FLASH_CLK_MAX_HZ 25000000u

#if !defined(SS_LINK_HEADER)
#  error "k21fn1m0 offers LINK=header only - see mcufw/k21fn1m0/board.mk"
#endif

const char *ssb_name(void) { return "k21fn1m0"; }

/* ---- the accelerator --------------------------------------------------
 * The packing is regs.h's; read it there. What is here is the apply
 * sequence and the refusal rule.
 *
 * TRAP 1, REFUSE WHAT THE CLOCK CANNOT RUN. The usual shape of this trap -
 * under-programming a latency field so that the next instruction fetch is
 * served too fast - CANNOT HAPPEN on this part, because the wait state is
 * read-only and the FMC computes it. The same hazard arrives by the other
 * door instead: the flash clock itself is capped, and the packed word
 * carries OUTDIV4. So the check below is on the flash clock the requested
 * divider would produce, and at the reset core clock every divider from 1
 * to 16 passes it. It starts refusing as soon as somebody builds this with
 * SS_CLK_MHZ above 25, which is the configuration the rig reaches with the
 * PLL. A refused word leaves the hardware exactly as it was and
 * SS_V_SET_ACR answers with the readback, so the caller sees the old value
 * come back.
 *
 * TRAP 2, THE CACHE DANCE. On this part it is not the disable-reset-enable
 * dance the ST parts need. CINV_WAY and S_B_INV are write-only PULSES in
 * the same register as the enables, read back as zero, and they are ORed
 * into every write exactly as rigs/fw_k21_aes/main.c does it: CINV_WAY
 * clears all four cache ways, S_B_INV clears the speculation buffer and the
 * single entry buffer. Not optional between cells - stale lines from the
 * previous configuration would otherwise serve the first traces of the next
 * one.
 *
 * THE APPLY RUNS FROM SRAM. RM S28.4, quoted in the rig: "Program the
 * registers only while the flash controller is idle (for example, execute
 * from RAM). Changing configuration settings while a flash access is in
 * progress can lead to non-deterministic behavior." memory.ld carries the
 * .ramfunc section and startup.c copies it. The rig keeps the FMC write and
 * the divider write in two such functions; they are fused into one here
 * because the contract has one entry point, and the order is the rig's -
 * divider first, then the FMC write with the invalidate pulses, so that
 * nothing is cached under the old timing and served under the new.
 */
__attribute__((section(".ramfunc"), noinline, used))
static void k21_apply(uint32_t ctl, uint32_t div4field)
{
    const uint32_t inv = (0xFu << FMC_PFB_CINV_WAY_Pos) | FMC_PFB_S_B_INV;
    uint32_t c;

    __asm volatile("cpsid i" ::: "memory");

    c = SIM_CLKDIV1 & ~SIM_CLKDIV1_OUTDIV4_Msk;
    SIM_CLKDIV1 = c | (div4field << SIM_CLKDIV1_OUTDIV4_Pos);
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");

    FMC_PFB0CR = ctl | inv;
    FMC_PFB1CR = ctl | inv;
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");

    __asm volatile("cpsie i" ::: "memory");
}

static int acr_legal(uint32_t v)
{
    uint32_t div4;

    if (v & K21_ACR_RESERVED_Msk)
        return 0;

    div4 = ((v & K21_ACR_OUTDIV4_Msk) >> K21_ACR_OUTDIV4_Pos) + 1u;
    if ((SS_CLK_HZ / div4) > K21_FLASH_CLK_MAX_HZ)
        return 0;

    return 1;
}

void ssb_acr_set(uint32_t v)
{
    uint32_t ctl, div4field;

    if (!acr_legal(v)) return;

    /* Bits 21..16 are the read-only fields. They are accepted and dropped
     * rather than refused, so that a host echoing back a readback word
     * still sets what it meant to set. */
    ctl  = v & K21_ACR_LOWBYTE_Msk;
    ctl |= ((v & K21_ACR_CLCK_WAY_Msk) >> K21_ACR_CLCK_WAY_Pos)
           << FMC_PFB_CLCK_WAY_Pos;
    div4field = (v & K21_ACR_OUTDIV4_Msk) >> K21_ACR_OUTDIV4_Pos;

    k21_apply(ctl, div4field);
}

/* Reassembled from the registers as they READ, never from what was asked
 * for. Bank 0 supplies the writable fields because ssb_acr_set writes both
 * banks the same; bank 0 also supplies the two read-only fields, which are
 * a property of the clock ratio and the flash array rather than of the
 * configuration. */
uint32_t ssb_acr_get(void)
{
    uint32_t p0 = FMC_PFB0CR;
    uint32_t c1 = SIM_CLKDIV1;
    uint32_t v;

    v  = p0 & K21_ACR_LOWBYTE_Msk;
    v |= ((p0 >> FMC_PFB_CLCK_WAY_Pos) & 0xFu) << K21_ACR_CLCK_WAY_Pos;
    v |= ((c1 & SIM_CLKDIV1_OUTDIV4_Msk) >> SIM_CLKDIV1_OUTDIV4_Pos)
         << K21_ACR_OUTDIV4_Pos;
    v |= ((p0 & FMC_PFB_B0RWSC_Msk) >> FMC_PFB_B0RWSC_Pos)
         << K21_ACR_B0RWSC_Pos;
    v |= ((p0 & FMC_PFB_B0MW_Msk) >> FMC_PFB_B0MW_Pos) << K21_ACR_B0MW_Pos;

    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }

/* One clock here. The rig can reach 120 MHz through the PLL, but doing so
 * needs crystal parameters that are a property of the board and that this
 * port has no measured values for, so it would be a guess with a plausible
 * wrong answer at the end of it. Returning 0 makes the host treat a clock
 * change as "not available on this part" rather than as a failure. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The FMC exposes all sixteen cache tags as readable registers
 * (FMC_TAGVDW*), which is unusual and useful, but tags are residency, not
 * counters. There is no hit or miss counter in this part's map, so the
 * honest answer to the contract's question is none, and the host reports
 * none instead of inventing numbers. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- the console ------------------------------------------------------
 * NOT A UART, AND THIS IS THE LARGEST GAP IN THE PORT.
 *
 * The TWR-K21F120M has no usable serial bridge: its only USB-serial path is
 * the on-board OSJTAG virtual port, which is USB vendor-class rather than
 * CDC, has no driver, and whose vendor toolkit is discontinued. The rig
 * therefore has no text protocol at all - its whole interface is a mailbox
 * struct in SRAM_U, written and read over SWD by a J-Link between batches,
 * and that is also the better arrangement for the measurement because the
 * host never touches the bus inside a timed region.
 *
 * So this board layer has no register source for a console. The part has
 * UARTs; the rig never brings one up, no pin assignment for this board has
 * been established here, and writing down a base address and a pin mux that
 * nobody has run would be exactly the wrong-value-that-looks-like-a-number
 * failure this repository exists to prevent.
 *
 * What is here instead is the rig's arrangement fitted to the contract: a
 * ring buffer pair in SRAM, which a debugger drives. It invents no register
 * address, because there isn't one - it is ordinary memory. ssb_put blocks
 * while the outbound ring is full and ssb_get blocks while the inbound one
 * is empty, which is the same shape as waiting on TXE and RXNE, and
 * blocking rather than dropping is deliberate: a dropped byte desynchronises
 * the protocol framing, and a host reading a short response goes looking at
 * the link rather than at the victim.
 *
 * WHAT THIS MEANS IN PRACTICE: THE HOST SIDE DOES NOT EXIST. tools/
 * speaks the protocol over a serial port, nothing in this tree drives this
 * ring over SWD, and until something does, a flashed image will come up,
 * disable its watchdog, configure the FMC and then block on the first byte
 * of the first command. The port is good for building and for the layout
 * axis; it cannot take a capture.
 *
 * BOARD_LINKS says `header`, which is a misnomer here. build.mk accepts
 * only `vcp` or `header`, and of the two, "not the virtual COM port" is the
 * less wrong description of an SWD mailbox.
 */
#define SSB_MBOX_MAGIC 0x4B323146u            /* "K21F", as the rig's is */
#define SSB_MBOX_TX_LEN 256u
#define SSB_MBOX_RX_LEN 64u

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t tx_head;                /* victim writes, host reads  */
    volatile uint32_t tx_tail;                /* host writes, victim reads  */
    volatile uint32_t rx_head;                /* host writes, victim reads  */
    volatile uint32_t rx_tail;                /* victim writes, host reads  */
    volatile uint8_t  tx[SSB_MBOX_TX_LEN];
    volatile uint8_t  rx[SSB_MBOX_RX_LEN];
} ssb_mbox_t;

/* Not static, and not at a pinned address: the host finds it by symbol in
 * the ELF, the same way it finds the table addresses. Pinning it would need
 * another output section in memory.ld for no gain. */
__attribute__((used)) ssb_mbox_t ssb_mbox;

static void console_init(void)
{
    ssb_mbox.tx_head = 0u;
    ssb_mbox.tx_tail = 0u;
    ssb_mbox.rx_head = 0u;
    ssb_mbox.rx_tail = 0u;
    __asm volatile("dsb" ::: "memory");
    ssb_mbox.magic = SSB_MBOX_MAGIC;          /* last: it is the ready flag */
}

void ssb_put(uint8_t c)
{
    uint32_t next = (ssb_mbox.tx_head + 1u) % SSB_MBOX_TX_LEN;

    while (next == ssb_mbox.tx_tail) {}
    ssb_mbox.tx[ssb_mbox.tx_head] = c;
    __asm volatile("dsb" ::: "memory");
    ssb_mbox.tx_head = next;
}

int ssb_get(void)
{
    uint8_t c;

#if defined(SS_TIMING_DWT)
    /* Re-armed here rather than in init, because a debug probe clears
     * DEMCR.TRCENA when it DISCONNECTS, which is after the board has
     * booted. Arming once at boot arms it and then has it switched off
     * again, and the liveness check has already passed by then, so the
     * device reports a counter it no longer has and every window comes out
     * zero - which reads exactly like a constant-time result. A console
     * read is outside every measured region, so this costs nothing. */
    if (!(DEMCR & DEMCR_TRCENA)) {
        DEMCR |= DEMCR_TRCENA;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
        __asm volatile("dsb; isb");
    }
#endif

    while (ssb_mbox.rx_tail == ssb_mbox.rx_head) {}
    c = ssb_mbox.rx[ssb_mbox.rx_tail];
    __asm volatile("dsb" ::: "memory");
    ssb_mbox.rx_tail = (ssb_mbox.rx_tail + 1u) % SSB_MBOX_RX_LEN;
    return (int)c;
}

/* ---- trigger ----------------------------------------------------------
 * NO TRIGGER PIN ON THIS PORT, and these are deliberately empty rather
 * than driving a plausible GPIO.
 *
 * The rig brings up no GPIO at all - it has no pin writes, no port clock
 * gating and no pin mux anywhere in it, because it is timed on-device by
 * CYCCNT and read out over SWD. So there is no established pin for the
 * external-instrument arm on this board, and the contract allows exactly
 * two register writes inside the measured window, which makes a guessed pin
 * the worst possible place for one: the wrong pin writes to the wrong
 * peripheral inside the window being measured, and the result is a number.
 *
 * Empty functions are safe here - the arm that carries a marker is the one
 * that calls them, and on this part that arm does not exist yet. Finding
 * the pin is a bench exercise, not a reading exercise.
 */
void ssb_trig_high(void) { }
void ssb_trig_low(void)  { }

/* ---- the cycle counter ------------------------------------------------
 * TRAP 3, THE BOARD OWNS THE SUBTRACTION, and the two counters go opposite
 * ways: DWT CYCCNT counts UP over 32 bits, SysTick counts DOWN over 24.
 *
 * Both arms check that the counter actually MOVED before claiming to have
 * one. CYCCNT can stop while CYCCNTENA still reads set, and a SysTick that
 * was never given a reload value sits at zero; either way every derived
 * interval comes out exactly zero, which is indistinguishable from a
 * constant-time result at the host. ssb_has_cyc reports what the liveness
 * check found, not what the register bits claim.
 */
#if defined(SS_TIMING_DWT)
static int g_cyc_ok = 0;

int      ssb_has_cyc(void)  { return g_cyc_ok; }
uint32_t ssb_cyc_read(void) { return DWT_CYCCNT; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return second - first;                 /* counts UP, full 32 bits */
}

static void cyc_init(void)
{
    uint32_t a, b;

    DEMCR |= DEMCR_TRCENA;
    if (DWT_CTRL & DWT_CTRL_NOCYCCNT) { g_cyc_ok = 0; return; }
    DWT_CYCCNT = 0u;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    __asm volatile("dsb; isb");
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
    /* The watchdog was already disabled by Reset_Handler, inside the 256
     * bus clock window the unlock sequence allows. It cannot be done here.
     *
     * The accelerator is put into its reset-equivalent state through the
     * same path the host will use, so that the boot state and a host-set
     * state are produced by identical code including the invalidate pulses.
     * K21_ACR_RESET is the measured reset configuration, not the reference
     * manual's stated reset value - the manual says PFB0CR resets to
     * 0x3004001F and the part reads 0x1004001F, and the part is right,
     * because the two differ only in the derived wait state and 3 is the
     * 4:1 clock ratio case. */
    ssb_acr_set(K21_ACR_RESET);
    console_init();
    cyc_init();
}
