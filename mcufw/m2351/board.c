/* Nuvoton M2351 (M2351KIAAE) board layer, Cortex-M23 at 12 MHz.
 *
 * THIS PORT IS UNTESTED ON SILICON. It has been compiled and nothing else: no
 * board was attached, nothing was flashed, no capture was taken, and no number
 * it produces has been checked against anything. Every address and bit
 * position in regs.h was copied from a rig that did run on this die
 * (rigs/fw_m2351_aes/ in the flash-accelerator-tests tree), but the
 * arrangement of them here is new code, and the places where this port could
 * not establish something are called out at the point they bite rather than
 * filled in. Walk the docs/PORTING.md checklist end to end before believing a
 * number from it.
 *
 * What is different about this part, and why this file does not look like the
 * ST ports:
 *
 *   THE WHOLE FLASH READ PATH IS ONE REGISTER, AND IT IS BOTH AXES AT ONCE.
 *   FMC_CYCCTL holds the wait state and, by the vendor's own description, the
 *   only documented way to turn the cache off - CYCLE = 0, which also caps
 *   HCLK below 27 MHz. So on this part "disable the accelerator" and "run at
 *   speed" are mutually exclusive by construction. They turn out to be
 *   mutually exclusive in a harder way than that: CYCLE = 0 cannot be written
 *   at all. See ssb_acr_set.
 *
 *   EVERY ACCELERATOR AND CLOCK WRITE IS PROTECTED. SYS_REGLCTL has to be
 *   unlocked first and a write without it is silently ignored, not faulted.
 *
 *   THERE IS NO CYCLE COUNTER BEYOND SysTick. ARMv8-M Baseline makes DWT
 *   optional and this die does not have it.
 *
 *   THERE IS NO UART AND NO TRIGGER PIN, and neither is a choice this port
 *   made. The console and trigger sections say what that costs.
 */
#include <stdint.h>
#include "regs.h"
#include "ss_board.h"
#include "ss_victim.h"

/* 12 MHz from HIRC, which is what the part ships doing and the clock every
 * published number for this die was taken at. The clock is fixed at build
 * time: the rig reaches 24, 48 and 64 MHz as well, but each of those needs
 * HIRC48 or the PLL brought up and the wait state carried through an
 * auto-tune transit on the way, which ssb_set_mhz cannot express. See
 * ssb_set_mhz. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 12
#endif
#if SS_CLK_MHZ != 12
#error "only 12 MHz is reproduced here: the other stops need HIRC48 or the PLL, see ssb_set_mhz"
#endif
#define SS_CLK_HZ (SS_CLK_MHZ * 1000000u)

/* The lowest CYCLE this clock may run, from the CYCCTL field table: CYCLE 1
 * is specified below 27 MHz, 2 for 25..52 MHz, 3 for 49..79 MHz.
 *
 * THE FLOOR IS 1 AND NOT 0 EVEN THOUGH 0 IS DOCUMENTED AT THIS CLOCK. CYCLE =
 * 0 is the vendor's "cache is disabled by hardware" setting and it does not
 * exist in silicon: an unclamped poke of FMC_CYCCTL on this die wrote 0x000
 * and read back 0x001, while every reserved value 4..15 stuck. The field
 * floors at 1. research/targets/m2351.md section 3 has the whole table. */
#if SS_CLK_MHZ > 52
#define CYCLE_FLOOR 3u
#elif SS_CLK_MHZ > 27
#define CYCLE_FLOOR 2u
#else
#define CYCLE_FLOOR 1u
#endif

const char *ssb_name(void) { return "m2351"; }

/* ---- the register lock -------------------------------------------------
 * Straight from the rig, including the retry loop: the unlock is three byte
 * writes in sequence and the status bit is what says it took. */
static void reglctl_unlock(void)
{
    while ((SYS_REGLCTL & REGLCTL_UNLOCKED) == 0u) {
        SYS_REGLCTL = 0x59u;
        SYS_REGLCTL = 0x16u;
        SYS_REGLCTL = 0x88u;
    }
}

static void reglctl_lock(void) { SYS_REGLCTL = 0x00u; }

/* ---- the accelerator ---------------------------------------------------
 * The word is FMC_CYCCTL masked to CYCLE and FADIS; regs.h says why nothing
 * is packed in from elsewhere.
 *
 * REFUSE WHAT THE CLOCK CANNOT RUN rather than execute the next instruction
 * fetch under it. This is trap 1 in ss_board.h, and the code doing the
 * reconfiguration is itself in flash, so an under-programmed CYCLE corrupts
 * the fetch of the instruction after the write: a hardfault mid-write, not a
 * wrong answer later. SS_V_SET_ACR replies with the readback, so a caller
 * learns a word was refused by getting the old one back.
 *
 * Three ways a word is refused here, and only the first is a clock rule:
 *
 *   CYCLE below the floor for this clock. The real one.
 *
 *   CYCLE = 0. Documented, unwritable on this die, and refused rather than
 *   written so that the capture records a refusal instead of a configuration
 *   that silently became CYCLE = 1 under the name 0x100. This is the one
 *   refusal that is a measurement about this silicon rather than a rule from
 *   the field table, and it is the reason there is no cache-off arm on this
 *   part at all.
 *
 *   CYCLE above 3. Values 4..15 are reserved, and on this die they STICK -
 *   the unclamped poke wrote 0x10F and read 0x010F back. The field table
 *   gives them no clock band and no meaning, so a sweep that recorded one
 *   would be recording a number nobody can name. Refused deliberately; this
 *   is a choice, not a silicon limit, and it is the one to revisit if a
 *   wait-state ladder past 3 is ever wanted.
 */
static int acr_legal(uint32_t v)
{
    uint32_t cycle = v & M2351_ACR_CYCLE_Msk;

    if (v & ~M2351_ACR_Msk)
        return 0;                            /* bits that are not in the word */

    /* With FADIS clear the hardware owns CYCLE, so the CYCLE bits of the
     * request are not a request for anything. The readback is what settles
     * what the part is running, and at this clock it has been measured to be
     * 1 - which is why the auto word in the sweep list is 0x001 and not
     * 0x000. Nothing to check here. */
    if (!(v & M2351_ACR_FADIS))
        return 1;

    if (cycle < CYCLE_FLOOR) return 0;
    if (cycle > 3u)          return 0;
    return 1;
}

void ssb_acr_set(uint32_t v)
{
    if (!acr_legal(v)) return;

    reglctl_unlock();

    if (v & M2351_ACR_FADIS) {
        /* Take ownership at whatever value the hardware had auto-tuned to
         * FIRST, then move it. The rig does it in these two writes for a
         * reason: a single write that sets FADIS and a new CYCLE together
         * would, going down in CYCLE, present the lower value and software
         * ownership in the same instant. Two writes never pass through
         * "fixed, too low, and already running". */
        FMC_CYCCTL |= M2351_ACR_FADIS;
        DSB(); ISB();
        FMC_CYCCTL = M2351_ACR_FADIS | (v & M2351_ACR_CYCLE_Msk);
        DSB(); ISB();

        /* FADIS is written in FMC_CYCCTL and mirrored read-only in
         * FMC_ISPSTS.FCYCDIS. Reading the mirror is what tells a protected
         * write that took from one that was ignored, and this port hands
         * CYCLE back to the hardware when the mirror disagrees, so that the
         * readback reports what the part is doing instead of a bit that
         * stuck in a register the hardware is not honouring.
         *
         * UNTESTED, AND THE FIRST THING TO CHECK ON SILICON. If a legal word
         * comes back refused here, the mirror's update timing relative to
         * this read is the suspect, not the write. */
        if (!(FMC_ISPSTS & ISPSTS_FCYCDIS)) {
            FMC_CYCCTL &= ~M2351_ACR_FADIS;
            DSB(); ISB();
        }
    } else {
        FMC_CYCCTL &= ~M2351_ACR_FADIS;
        DSB(); ISB();
    }

    reglctl_lock();
}

/* READ the register back, never reconstruct it from the request. On this part
 * that is doing more work than usual: it is what shows CYCLE = 0 arriving as
 * 1, and it is the only thing that would show a protected write having been
 * ignored. */
uint32_t ssb_acr_get(void) { return FMC_CYCCTL & M2351_ACR_Msk; }

uint32_t ssb_clk_hz(void) { return SS_CLK_HZ; }

/* Reported as unavailable rather than half done. The clock is an axis on this
 * part and a real one - the rig exposes five stops, two of them the same
 * frequency from different sources - but every move couples to the wait
 * state: the transition has to hand CYCLE back to the hardware, change the
 * clock, and only then apply a fixed CYCLE, so that it never passes through
 * fixed-too-low-and-fast. This hook takes only MHz and cannot express that,
 * and CYCLE_FLOOR above is folded at build time, so a hook that moved the
 * clock would leave ssb_acr_set policing the wrong floor and produce
 * plausible numbers under an illegal configuration. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* The part has a 4 KB flash cache and no counters for it. That is not an
 * omission in this file: no register in the map names the cache at all, let
 * alone counts its hits, which research/targets/m2351.md section 3 records as
 * a property of the part. Saying 0 makes the host report "none" rather than
 * invent numbers. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock -------------------------------------------------------------
 * HIRC at 12 MHz with HCLKDIV = /1, which is the state the part resets into
 * and the state the rig deliberately leaves alone. It is asserted rather than
 * assumed here for one reason: every cycle count this victim reports is in
 * HCLK ticks, so a part that came up on a different source would report
 * numbers that are wrong by a ratio and wrong in no other way.
 *
 * Divider before source, which is the rig's order and matters going the other
 * way: setting the source first can momentarily present an undivided fast
 * clock to the flash.
 *
 * INSIDE THE UNLOCK WINDOW, because the rig writes the clock registers inside
 * one. Whether CLKSEL0 and CLKDIV0 are protected on this part is not
 * established here, and unlocking costs three byte writes.
 */
static void clock_init(void)
{
    reglctl_unlock();

    CLK_PWRCTL |= PWRCTL_HIRCEN;
    while ((CLK_STATUS & STATUS_HIRCSTB) == 0u) { }

    CLK_CLKDIV0 &= ~CLKDIV0_HCLKDIV_Msk;
    DSB(); ISB();
    CLK_CLKSEL0 = (CLK_CLKSEL0 & ~CLKSEL0_HCLKSEL_Msk) | HCLKSEL_HIRC;
    DSB(); ISB();

    reglctl_lock();
}

/* ---- the console -------------------------------------------------------
 * THIS IS NOT A UART AND THE HOST SIDE OF IT DOES NOT EXIST YET. Read this
 * before wiring anything up.
 *
 * This part has no serial link to the host, and unlike every ST part here
 * that is not a driver problem somebody could fix. The Nu-Link ICE's second
 * USB interface is a bare CDC-DATA descriptor, class 0x0A, with no class-02
 * control interface in front of it; usbser.sys binds to the control
 * interface, so nothing can ever bind this one and it sits at PnP problem
 * code 28 permanently. The rig that measured this die is therefore driven
 * entirely over SWD through a mailbox in SRAM and transmits nothing during a
 * measured region.
 *
 * So there is no UART driver in that rig to copy, and writing UART base
 * addresses and bit positions out of a family datasheet is exactly the guess
 * this repository exists to prevent. It would not even fail loudly: this
 * probe never reports a bus fault - a read of an unmapped address returns
 * 0xFFFFFFFF, or the previous response, or a plausible constant - so a wrong
 * peripheral address here produces a victim that hangs or answers rubbish,
 * and the host reports a short response and blames the link.
 *
 * The console is therefore a byte-stream mailbox in .bss with the same
 * blocking shape a UART has, found by the host through the `ss_link` symbol
 * in the ELF. The firmware owns tx_head and rx_tail, the host owns tx_tail
 * and rx_head, and each side only ever advances its own index, which is what
 * makes it safe to poke over SWD while the core runs.
 *
 * It is found by SYMBOL and not pinned at an address, which is the one way
 * this differs from the rig's mailbox. The rig pins its own at 0x20000000
 * with a section in its link script; the only linker file this port owns is
 * memory.ld, and pinning would mean carving the window out of the RAM region
 * and leaving a hole the shared sections script knows nothing about.
 *
 * WHAT THIS MEANS IN PRACTICE: a victim built from this port comes up,
 * configures the part, and then blocks in ssb_get forever, because nothing on
 * the host speaks to the mailbox. Making this part usable needs a host-side
 * SWD transport for the protocol - results/m2351/nulink.py in the
 * flash-accelerator-tests tree is the probe half of one - and that is a
 * change to tools/capture.py, which is outside this port.
 *
 * BOARD_LINKS says `vcp` because build.mk accepts only `vcp` or `header`. On
 * this part that word names the debug-probe link and not a virtual COM port,
 * which is the same thing it means on the RA2A1.
 */
#if !defined(SS_LINK_VCP)
#error "this part offers only LINK=vcp, which here means the SWD mailbox - see board.mk"
#endif

#define SS_LINK_RING 256u

typedef struct {
    volatile uint32_t magic;                 /* "M35L", so a host can find it */
    volatile uint32_t tx_head;               /* firmware writes               */
    volatile uint32_t tx_tail;               /* host writes                   */
    volatile uint32_t rx_head;               /* host writes                   */
    volatile uint32_t rx_tail;               /* firmware writes               */
    volatile uint8_t  tx[SS_LINK_RING];
    volatile uint8_t  rx[SS_LINK_RING];
} ss_link_t;

ss_link_t ss_link;

/* Not the rig's 0x3135334D. The same magic on a different structure is worse
 * than no magic: a host that knows the rig's mailbox would find this one,
 * recognise it and read the wrong fields out of it. */
#define SS_LINK_MAGIC 0x4C35334Du            /* 'M','3','5','L' little endian */

static void link_init(void)
{
    ss_link.tx_head = 0u;
    ss_link.tx_tail = 0u;
    ss_link.rx_head = 0u;
    ss_link.rx_tail = 0u;
    DSB();
    ss_link.magic = SS_LINK_MAGIC;
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
 * NOT IMPLEMENTED, AND DELIBERATELY NOT FAKED. The contract wants two
 * register writes bracketing the cipher, and that needs a GPIO data register
 * plus whatever makes the pin an output. The rig that measured this part
 * drives no pin at all - its whole transport is the SWD mailbox - so this
 * port has no measured address for either, and this file contains no GPIO
 * block to write one from.
 *
 * The consequence is specific: a build from this port can be timed by the
 * on-chip SysTick and cannot be timed by an external instrument, because
 * there is no edge for one to trigger on. There is no ssb_has_trig in the
 * contract, so nothing reports that to the host; a capture taken against an
 * external scope here would record the window it asked for and see nothing.
 */
void ssb_trig_high(void) { }
void ssb_trig_low(void)  { }

/* ---- the cycle counter -------------------------------------------------
 * SysTick, because there is nothing else. THERE IS NO DWT ON THIS DIE: the
 * rig writes DEMCR.TRCENA and DWT_CTRL.CYCCNTENA, does a little work and
 * reports the delta, and on this part DWT_CTRL reads 0x00000000 and the delta
 * is 0. A DWT build here would time every window as exactly zero, which reads
 * as a constant-time result rather than as a missing counter, so it is
 * refused at compile time instead of allowed to produce that.
 *
 * Note the shape of the evidence, because the cheaper check is worthless:
 * DWT_CTRL.NOCYCCNT reading clear proves nothing when the whole register
 * reads as zero. The rig probes by writing the enables and watching whether
 * the counter MOVES.
 *
 * SysTick counts DOWN over 24 bits at HCLK with 1-cycle resolution. At 12 MHz
 * it wraps every 1.4 s, against an AES block of order 100 us.
 *
 * ssb_cyc_delta OWNS THE SUBTRACTION and this is the direction that makes it
 * necessary: `second - first` would give large positive garbage rather than
 * an error. Trap 3 in ss_board.h.
 */
#ifdef SS_TIMING_DWT
#error "TIMING=dwt is impossible on this die: DWT_CTRL reads 0 and the counter does not move"
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

/* THE CACHE DANCE OF TRAP 2 CANNOT BE DONE ON THIS PART, which is worth
 * stating here because its absence looks like an omission. There is no cache
 * enable to clear, no invalidate bit to pulse and no register that names the
 * cache, so a configuration change cannot leave the structure in the state a
 * reset-time write would: lines allocated under the previous CYCLE stay
 * resident. The only thing that reaches a known state is a run of the cipher,
 * which is what the warm-up encryption in ss_victim.c already is.
 *
 * ssb_init leaves FMC_CYCCTL alone, as the rig does, so the part comes up in
 * the configuration it ships in - FADIS clear, CYCLE auto-tuned, measured as
 * 1 at this clock - and the reset configuration stays measurable instead of
 * being something this firmware has already changed.
 */
void ssb_init(void)
{
    clock_init();
    link_init();

#ifdef SS_TIMING_SYSTICK
    SYST_RVR = SYST_MASK;
    SYST_CVR = 0;
    SYST_CSR = SYST_ENABLE_CORECLK;
#endif
}
