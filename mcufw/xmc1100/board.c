/* Infineon XMC1100-Q024F0064 board layer. Cortex-M0, ARMv6-M, 16 KB SRAM.
 *
 * THIS PORT HAS NEVER RUN ON SILICON. It was written against
 * rigs/fw_xmc1_aes/ in the research tree, which has, and it compiles and
 * links for both table residences. Nothing below has been observed on a
 * part through this code path, and two of the hooks the contract asks for
 * are not implemented at all. Read the next two paragraphs before planning
 * a capture with it.
 *
 * THE CONSOLE IS NOT IMPLEMENTED, AND THAT IS THE FIRST THING TO FIX.
 * This part's serial peripheral is a USIC channel in ASC (UART) mode, which
 * is not an ST-style USART with an SR, a DR, a BRR and a CR1 at a base
 * address, and none of its registers, bit positions or baud-rate
 * arithmetic appear in the rig: that rig talks to its host over an SWD
 * mailbox and says so in as many words - "THERE IS NO UART TO THE HOST --
 * by choice, not by absence". Writing a USIC from memory would produce a
 * victim that comes up, answers nothing, and reads from the host side as a
 * baud or framing problem, which is the exact failure docs/PORTING.md
 * warns about under "a register that is not there HANGS, and the host
 * blames the UART". So ssb_put discards and ssb_get never returns, both
 * loudly, and the build warns. What a USIC port needs is the channel base,
 * the fractional divider and the pin routing, taken off hardware or a
 * vendor header, not guessed.
 *
 * THE TRIGGER PIN IS NOT IMPLEMENTED EITHER, for the same reason: the rig
 * drives no GPIO, so no port base address or pin for the XMC2Go header is
 * established anywhere this port could read it. ssb_trig_high and
 * ssb_trig_low are empty. Note that SS_V_IDENT still advertises
 * SS_VI_HAS_TRIGGER, because the shared protocol layer sets that flag
 * unconditionally and a board file cannot clear it. An externally timed
 * measurement through this port would therefore be triggerless while the
 * device claims otherwise, which is worth fixing in the same pass as the
 * console.
 *
 * What IS implemented and is taken register for register from the rig: the
 * flash read-path word, the clock report, and the SysTick time base. See
 * regs.h for the packing and for why there is no accelerator here to pack.
 */
#include <stdint.h>
#include "regs.h"
#include "ss_board.h"
#include "ss_victim.h"

#warning "xmc1100: the console and the trigger are NOT implemented - see the comment at the top of board.c"

const char *ssb_name(void) { return "xmc1100"; }

/* ---- the clock ---------------------------------------------------------
 * NOT TOUCHED BY THIS PORT, deliberately, and reported by reading the
 * divider rather than by asserting a number.
 *
 * On this board the ROM startup software raises MCLK before user code
 * runs: the register reset value of CLKCR is 0x3FF00400 (IDIV = 4, 8 MHz)
 * but the SSW leaves 0x3FF00100 (IDIV = 1, 32 MHz, the rated maximum), and
 * a capture taken at one while the write-up says the other is a whole
 * round of results. Computing it from CLKCR means the device reports what
 * it is actually running at, including the case where something else got
 * there first - the factory image on this die had divided itself back down
 * to 8 MHz.
 *
 * MCLK = 64 MHz / (2 * (IDIV + FDIV/256)). The 64 MHz DCO is measured, not
 * assumed: the rig host-timed 31.977 / 15.996 / 8.010 MHz for IDIV 1/2/4.
 */
uint32_t ssb_clk_hz(void)
{
    uint32_t c = SCU_CLKCR;
    uint32_t idiv = (c & CLKCR_IDIV_Msk) >> CLKCR_IDIV_Pos;
    uint32_t den = 2u * (idiv * 256u + (c & CLKCR_FDIV_Msk));

    if (den == 0u) return 0u;              /* IDIV = FDIV = 0 is not a clock */
    return (uint32_t)(((uint64_t)XMC_DCO_HZ * 256u) / den);
}

/* The part CAN change MCLK through CLKCR.IDIV, and the rig does. It is
 * reported as unavailable here because the clock and the wait state are
 * coupled through the flash timing parameter, so a clock change has to
 * carry the read-path word with it or leave the part in a configuration it
 * cannot fetch under - the rig does both in one function for that reason.
 * This hook cannot report the coupled refusal, so the clock stays a
 * property of whatever the SSW left, which ssb_clk_hz reads out. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* ---- the flash read-path word -----------------------------------------
 * There is no accelerator on this part and no cache to do trap 2's dance
 * with. The word is WS, FIXWS and HRLEV packed as regs.h documents.
 *
 * TRAP 1 IS LIVE HERE AND IT IS MEASURED, NOT DEFENSIVE. Fixed WS = 0 at
 * 32 MHz was tried on this die and the part stopped answering: instruction
 * fetch needs the wait state at that clock and the code that would undo the
 * setting is itself in flash. A J-Link reset recovers it, because NVMCONF
 * and CONFIG1 are SFRs and come back to 0x9000 and adaptive, but it costs a
 * reconnect in the middle of a sweep. So WS = 0 is refused whenever
 * IDIV < 2, which is the same clamp the rig applies, and the caller learns
 * it was refused because SS_V_SET_ACR replies with the readback. WS = 0 is
 * fine at 16 MHz and below and is not refused there.
 *
 * A word with any reserved bit set is refused outright rather than masked,
 * so an accelerator word meant for a different part cannot be half-applied
 * here and then recorded under the name it was asked for.
 */
static int acr_legal(uint32_t v)
{
    uint32_t idiv;

    if (v & ~XMC_ACR_KNOWN_Msk) return 0;

    idiv = (SCU_CLKCR & CLKCR_IDIV_Msk) >> CLKCR_IDIV_Pos;
    if (!(v & XMC_ACR_WS) && idiv < 2u) return 0;

    return 1;
}

/* THE ORDER IS THE WHOLE POINT, and it is the rig's. In the fixed scheme,
 * too few wait states for the current MCLK corrupts the fetch of the code
 * doing the reconfiguration, so the failure is a fault in the middle of the
 * write sequence rather than a wrong answer later. The adaptive scheme is
 * safe at every frequency by construction, which is what it is for, so
 * every transition goes adaptive first, then the cost fields, then the
 * requested scheme, and never passes through fixed-and-too-fast.
 *
 * Both registers are read-modify-written. NVMCONF carries NVM_ON and the
 * protection bits, and a CONFIG1 write must touch only FIXWS. */
void ssb_acr_set(uint32_t v)
{
    if (!acr_legal(v)) return;

    NVM_CONFIG1 = NVM_CONFIG1 & ~CONFIG1_FIXWS;
    __asm volatile("dsb; isb");

    if (v & XMC_ACR_WS) NVM_NVMCONF = NVM_NVMCONF |  NVMCONF_WS;
    else                NVM_NVMCONF = NVM_NVMCONF & ~NVMCONF_WS;
    __asm volatile("dsb; isb");

    NVM_NVMCONF = (NVM_NVMCONF & ~NVMCONF_HRLEV)
                | ((v & XMC_ACR_HRLEV_Msk) >> XMC_ACR_HRLEV_Pos);
    __asm volatile("dsb; isb");

    if (v & XMC_ACR_FIXWS) NVM_CONFIG1 = NVM_CONFIG1 |  CONFIG1_FIXWS;
    __asm volatile("dsb; isb");
}

/* READ, never reconstructed from the request. That is what makes a refusal
 * visible instead of silently recorded under the name it was asked for. */
uint32_t ssb_acr_get(void)
{
    uint32_t conf = NVM_NVMCONF;
    uint32_t cfg1 = NVM_CONFIG1;
    uint32_t v = 0u;

    if (conf & NVMCONF_WS)    v |= XMC_ACR_WS;
    if (cfg1 & CONFIG1_FIXWS) v |= XMC_ACR_FIXWS;
    v |= (conf & NVMCONF_HRLEV) << XMC_ACR_HRLEV_Pos;

    return v;
}

/* No cache of any kind, so no hit/miss counters, and nothing on this die
 * that could stand in for them. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- the console, which does not exist on this port --------------------
 * See the banner at the top of this file. ssb_get blocks forever rather
 * than returning a byte that was never received, because a victim that
 * answers invented traffic is worse than one that visibly does nothing:
 * the protocol loop would desync, reply with errors, and look like a link
 * problem. A board with this image on it is silent, which is the truth. */
void ssb_put(uint8_t c)
{
    (void)c;
}

int ssb_get(void)
{
    for (;;) { }
}

/* ---- the trigger, which does not exist on this port either ------------- */
void ssb_trig_high(void) { }
void ssb_trig_low(void)  { }

/* ---- the time base -----------------------------------------------------
 * SysTick is the ONLY counter on this part. There is no DWT cycle counter:
 * ARMv6-M does not require one and Cortex-M0 does not implement one, and
 * the die lies about it - the NOCYCCNT bit reads clear, which is the "it is
 * there" encoding, and DWT_CTRL reads 0x20000000 through a debugger and
 * 0x0 from firmware because the probe sets DEMCR.TRCENA on connect and the
 * firmware does not inherit it. The rig wrote TRCENA, then CYCCNTENA, then
 * read DWT_CYCCNT twice around a spin: delta 0. So the DWT arm of this port
 * does not exist, and asking for it is a build error rather than a build
 * that times every encryption at zero and reads as constant time.
 *
 * SysTick counts DOWN over 24 bits, which is why ssb_cyc_delta owns the
 * subtraction (trap 3). At 32 MHz it wraps every 0.52 s, far longer than
 * one encryption, and the mask handles the wrap when one straddles it.
 *
 * CLKSOURCE is set to 1, the processor clock, which is MCLK and is what the
 * rig used and what the dossier's cycle counts are in. What SysTick counts
 * with CLKSOURCE = 0 on this part is NOT ESTABLISHED by anything this port
 * read, so that bit is written rather than left at its reset value.
 */
#if defined(SS_TIMING_DWT)
#  error "xmc1100 has no DWT cycle counter: Cortex-M0 does not implement one, and it was measured absent on this die. Build with TIMING=systick or TIMING=none."
#endif

#ifdef SS_TIMING_SYSTICK
int      ssb_has_cyc(void)  { return 1; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & SYST_MASK;   /* counts DOWN */
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

/* The clock is left exactly as the SSW set it, so the configuration a
 * capture records is the one the part ships in rather than one this file
 * has already changed. The read-path word is left alone for the same
 * reason: it comes up as NVMCONF 0x9000 and CONFIG1 FIXWS = 0, which is the
 * adaptive scheme, and the host moves it from there through SS_V_SET_ACR. */
void ssb_init(void)
{
#ifdef SS_TIMING_SYSTICK
    SYST_RVR = SYST_MASK;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_CLKSOURCE;
#endif
}
