/* Renesas RA2A1 (R7FA2A1AB3CFM) board layer, Cortex-M23 at 48 MHz.
 *
 * THIS PORT IS UNTESTED ON SILICON. It has been compiled and nothing else: no
 * board was attached, nothing was flashed, no capture was taken, and no number
 * produced by it has been checked against anything. Every address and bit
 * position in regs.h was copied from a rig that did run on the die
 * (rigs/fw_ra2_aes/ in the flash-accelerator-tests tree), but the arrangement
 * of them here is new code and the three places where this port could not
 * establish something are called out below rather than filled in. Treat the
 * first numbers off this board as suspect until the PORTING.md checklist has
 * been walked end to end.
 *
 * What is different about this part, and why the file does not look like the
 * ST ports:
 *
 *   THERE IS NO FLASH_ACR. The flash cache enable, the memory wait state and
 *   the flash wait field live in three registers across two peripherals, so
 *   the single opaque accelerator word is packed. The packing is documented at
 *   the top of regs.h and nowhere else.
 *
 *   THERE IS NO CYCLE COUNTER. DWT_CYCCNT is not implemented on this core, so
 *   SysTick is the time base and ssb_has_cyc reports what SysTick can do.
 *
 *   THERE IS NO UART TO THE HOST on the board this part was measured on, and
 *   no SCI register address is established by the rig, so the console here is
 *   a RAM mailbox rather than a serial port. See the console section.
 */
#include <stdint.h>
#include "regs.h"
#include "ss_board.h"
#include "ss_victim.h"

/* 48 MHz: HOCO with SCKDIVCR = ICLK/1, which is what the factory image on
 * this board was running when it arrived and what any build that takes the
 * part to its rated clock gets. It is fixed at build time here because the
 * clock and the wait state are coupled on this part and ssb_set_mhz cannot
 * express the coupling; see ssb_set_mhz. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 48
#endif
#if SS_CLK_MHZ != 48
#error "only 48 MHz is reproduced here: the divider and MEMWAIT are coupled, see ssb_set_mhz"
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

/* Above 32 MHz the part may not run with zero memory wait states, so at the
 * one clock this port builds for, MEMWAIT = 1 is mandatory. This is the
 * constant ssb_acr_set refuses against. */
#define MEMWAIT_MANDATORY (SS_CLK_HZ > 32000000u)

const char *ssb_name(void) { return "ra2a1"; }

/* ---- the accelerator --------------------------------------------------
 * Packing: bit 0 FCACHEEN, bit 4 MEMWAIT, bits 8..10 FLWT. regs.h says why.
 *
 * REFUSE WHAT THE CLOCK CANNOT RUN rather than execute the next instruction
 * fetch under it. On this part the illegal corner is MEMWAIT = 0 while ICLK is
 * above 32 MHz, which the FSP device header marks prohibited; it is the same
 * shape as the L152's zero wait states at 32 MHz, which stopped that part dead
 * mid-script with four checks already passed and looked like an unrelated
 * crash from the host. SS_V_SET_ACR replies with the readback, so a caller
 * learns a word was refused by getting the old one back. Trap 1 in ss_board.h.
 *
 * FLWT is not range-checked beyond its three bits because all eight values
 * were written on this die and all eight stuck (ra2a1.md section 1a), each
 * step above 1 adding about five cycles to a flash read. FCACHEEN has no
 * illegal value.
 */
static int acr_legal(uint32_t v)
{
    if (MEMWAIT_MANDATORY && !(v & RA_ACR_MEMWAIT))
        return 0;
    return 1;
}

/* THE CACHE DANCE IS REAL ON THIS PART AND THE RIG DOES IT, so it is
 * reproduced rather than invented: disable, invalidate, poll the
 * self-clearing invalidate bit, and only then re-enable. Renesas FCACHE keeps
 * stale lines across a reconfigure otherwise, which is the RA spelling of the
 * ICRST/DCRST pulse that every ST firmware on this bench got wrong until a
 * positive control caught it. Trap 2 in ss_board.h.
 *
 * The poll has no timeout, in line with the rest of the contract. If the bit
 * never clears the victim stops here and the host sees a short response, which
 * PORTING.md describes.
 */
static void fcache_set(int on)
{
    FCACHEE = 0;
    DSB();
    FCACHEIV = 1;
    while (FCACHEIV & 1u) { }
    if (on) FCACHEE = 1;
    DSB();
    ISB();
}

void ssb_acr_set(uint32_t v)
{
    if (!acr_legal(v)) return;

    /* FLWT first and MEMWAIT second, both before the cache is touched. Order
     * matters on this part only when the clock moves, which it does not here,
     * but the wait states still go in before the cache is re-enabled so the
     * cache is never refilled under a wait setting that is about to change.
     *
     * MEMWAIT sits behind the register protection key; FLWT is in the FCACHE
     * block, which the rig writes with no unlock at all. */
    FLWT = (uint8_t)((v & RA_ACR_FLWT_Msk) >> RA_ACR_FLWT_Pos);
    DSB();

    PRCR = PRCR_UNLOCK;
    MEMWAIT = (uint8_t)((v & RA_ACR_MEMWAIT) ? 1u : 0u);
    DSB();
    PRCR = PRCR_LOCK;
    ISB();

    fcache_set((v & RA_ACR_FCACHEEN) ? 1 : 0);
}

/* READ the three registers back. Never reconstruct this from the request:
 * reading is the only thing that makes a refusal visible, and on this part it
 * is also the only thing that would show FLWT behaving differently from the
 * value written, which is exactly the question section 1a of the dossier had
 * to run an experiment to answer. */
uint32_t ssb_acr_get(void)
{
    uint32_t v = 0u;

    if (FCACHEE & 1u)  v |= RA_ACR_FCACHEEN;
    if (MEMWAIT & 1u)  v |= RA_ACR_MEMWAIT;
    v |= ((uint32_t)FLWT & 7u) << RA_ACR_FLWT_Pos;
    return v;
}

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }

/* Reported as unavailable rather than half done. On this part the clock
 * divider and the wait state are one decision: MEMWAIT = 0 is prohibited above
 * 32 MHz, so lowering the clock widens the legal accelerator set and raising
 * it narrows it, and the order of the two writes is asymmetric because the
 * part must never pass through the prohibited state. The rig takes the divider
 * and the wait state together in a single command for that reason. This hook
 * takes only MHz and so cannot express it, and a hook that changed the clock
 * while leaving ssb_acr_set's legality constant folded at build time would
 * produce plausible numbers under an illegal configuration. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The part has a flash cache but no hit or miss counters exposed to software
 * that the rig found or used, so the honest answer is none. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock -------------------------------------------------------------
 * Straight from the rig, including the order, which is the part that matters:
 * MEMWAIT goes to 1 before anything speeds up, because the raise would
 * otherwise pass through the prohibited zero-wait state at 48 MHz. HOCO is
 * started and waited for, the part is put in high-speed operating mode and the
 * mode change waited for, the dividers are set to the factory image's
 * ICLK/1 PCLKB/2 FCLK/2, and only then is the source switched to HOCO.
 * PCLKB and FCLK keep their /2 so neither is overclocked as ICLK moves.
 */
static void clock_init(void)
{
    PRCR = PRCR_UNLOCK;

    MEMWAIT = 1u;
    DSB();

    HOCOCR = 0;                              /* HCSTP = 0: HOCO on          */
    while (!(OSCSF & 1u)) { }                /* HOCOSF: HOCO stable         */

    OPCCR = 0;                               /* high-speed operating mode   */
    while (OPCCR & OPCCR_OPCMTSF) { }        /* transition complete         */

    SCKDIVCR = SCKDIVCR_ICLK1_PCLKB2_FCLK2;
    SCKSCR = 0;                              /* CKSEL = HOCO                */
    DSB();

    PRCR = PRCR_LOCK;
    ISB();
}

/* ---- the console -------------------------------------------------------
 * THIS IS NOT A UART AND THE HOST SIDE OF IT DOES NOT EXIST YET. Read this
 * before wiring anything up.
 *
 * The board the RA2A1 was measured on has no serial link to the host at all:
 * its J-Link OB enumerates as a bare J-Link with no CDC interface, so the rig
 * is driven entirely over SWD through a mailbox in SRAM and transmits nothing
 * during a measured region. This port cannot copy an SCI driver out of that
 * rig because the rig does not contain one, and writing SCI base addresses and
 * bit positions from a family datasheet is exactly the guess this repository
 * exists to prevent: a wrong peripheral address on this part does not fault,
 * it hangs, and the host reports a short response and blames the UART.
 *
 * So the link is a byte-stream mailbox in .bss with the same blocking shape a
 * UART has, found by the host through the `ss_link` symbol in the ELF. The
 * firmware owns tx_head and rx_tail, the host owns tx_tail and rx_head, and
 * each side only ever advances its own index, which is what makes it safe to
 * poke over SWD while the core runs.
 *
 * WHAT THIS MEANS IN PRACTICE: a victim built from this port will come up,
 * configure the part and then block in ssb_get forever, because nothing on the
 * host speaks to the mailbox. Making this part usable needs one of two things,
 * neither of which is in scope here: a host-side SWD transport for the
 * protocol, or an SCI driver written from a datasheet and then verified on the
 * die against a loopback before any measurement is believed.
 *
 * BOARD_LINKS says `vcp` because build.mk accepts only `vcp` or `header`. On
 * this part that word names the debug-probe link, not a virtual COM port.
 */
#define SS_LINK_RING 256u

typedef struct {
    volatile uint32_t magic;                 /* "RA2L", so a host can find it */
    volatile uint32_t tx_head;               /* firmware writes               */
    volatile uint32_t tx_tail;               /* host writes                   */
    volatile uint32_t rx_head;               /* host writes                   */
    volatile uint32_t rx_tail;               /* firmware writes               */
    volatile uint8_t  tx[SS_LINK_RING];
    volatile uint8_t  rx[SS_LINK_RING];
} ss_link_t;

ss_link_t ss_link;

static void link_init(void)
{
    ss_link.tx_head = 0u;
    ss_link.tx_tail = 0u;
    ss_link.rx_head = 0u;
    ss_link.rx_tail = 0u;
    DSB();
    ss_link.magic = 0x4C324152u;             /* 'R','A','2','L' little endian */
}

void ssb_put(uint8_t c)
{
    uint32_t next = (ss_link.tx_head + 1u) % SS_LINK_RING;

    while (next == ss_link.tx_tail) { }      /* blocks like a full UART would */
    ss_link.tx[ss_link.tx_head] = c;
    DSB();
    ss_link.tx_head = next;
}

int ssb_get(void)
{
    uint8_t c;

    while (ss_link.rx_tail == ss_link.rx_head) { }
    c = ss_link.rx[ss_link.rx_tail];
    ss_link.rx_tail = (ss_link.rx_tail + 1u) % SS_LINK_RING;
    return (int)c;
}

/* ---- the trigger -------------------------------------------------------
 * NOT IMPLEMENTED, AND DELIBERATELY NOT FAKED. The contract wants two register
 * writes bracketing the cipher, and that needs one GPIO port register plus the
 * pin-function register that makes the pin an output. The rig that measured
 * this part drives no pin at all, so this port has no measured address for
 * either, and the RA pin-function registers are write-protected behind
 * PWPR/PFS, a sequence this port cannot reproduce from anything it has seen.
 *
 * The consequence is specific and worth stating: a build from this port can be
 * timed by the on-chip SysTick and cannot be timed by an external instrument,
 * because there is no edge for one to trigger on. There is no ssb_has_trig in
 * the contract, so nothing tells the host that; a capture taken with an
 * external scope against this port would record the window it asked for and
 * see nothing.
 */
void ssb_trig_high(void) { }
void ssb_trig_low(void)  { }

/* ---- the cycle counter -------------------------------------------------
 * SysTick, because there is nothing else. DWT_CYCCNT IS NOT IMPLEMENTED on
 * this core: on the die DWT_CTRL reads 0x2B000000, bit 25 NOCYCCNT set,
 * CYCCNTENA will not set, and three reads of DWT_CYCCNT with the core running
 * all return zero. A DWT build here would therefore time every window as
 * exactly zero, which reads as a constant-time result, so it is refused at
 * compile time rather than allowed to produce that.
 *
 * SysTick counts DOWN over 24 bits at ICLK with 1-cycle resolution, so it is
 * an exact substitute. At 48 MHz it wraps every 350 ms against an encryption
 * of order 150 us, about 2000 times the margin.
 *
 * ssb_cyc_delta OWNS THE SUBTRACTION and this is the direction that makes it
 * necessary: `second - first` would give large positive garbage rather than an
 * error. Trap 3 in ss_board.h.
 */
#ifdef SS_TIMING_DWT
#error "TIMING=dwt is impossible on this core: DWT_CYCCNT is not implemented (NOCYCCNT=1)"
#endif

#ifdef SS_TIMING_SYSTICK
int      ssb_has_cyc(void)  { return 1; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & SYST_MASK;     /* counts DOWN */
}
#else
int      ssb_has_cyc(void)  { return 0; }
uint32_t ssb_cyc_read(void) { return 0u; }
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

    /* The configuration the part ships in, which is the one worth booting
     * into: cache on, one memory wait, FLWT at its reset value of 0. It is
     * created by firmware rather than by the silicon, since at reset this part
     * is slow, uncached and at zero wait states. Going through ssb_acr_set
     * rather than writing the registers here keeps the invalidate dance and
     * the legality check in one place. */
    ssb_acr_set(RA_ACR_FCACHEEN | RA_ACR_MEMWAIT);

    link_init();

#ifdef SS_TIMING_SYSTICK
    SYST_RVR = SYST_MASK;
    SYST_CVR = 0;
    SYST_CSR = SYST_ENABLE_CORECLK;
#endif
}
