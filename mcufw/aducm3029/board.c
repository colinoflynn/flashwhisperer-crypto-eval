/* ADuCM3029 board layer. The whole part-specific half of a victim.
 *
 * UNTESTED ON SILICON. Nothing in this directory has been flashed, run, or
 * captured from. No ADuCM3029 board was attached while it was written, and
 * no pyocd target for the part exists to attach one with. What has been
 * checked is that it compiles and links for both table-residence variants
 * and fits the part's RAM. Every number in it was copied from
 * rigs/fw_aducm_aes/ in the research tree, which is the code that HAS run
 * on this silicon, and where the rig does not establish something this port
 * says so instead of supplying a value.
 *
 * Analog Devices Cortex-M3 at 26 MHz. The interesting thing about it for
 * this project is what it does NOT have: no wait-state field anywhere in
 * the memory map, no data cache, and so no accelerator knob a defender
 * could turn. The only read-path bit is an instruction-cache enable. The
 * packed accelerator word and the reasoning are at the top of regs.h.
 *
 * Everything part-specific is in this file; the protocol loop
 * (instrumentation/victim/src/ss_victim.c) and the cipher
 * (eval/lib/<name>/provider.c) mention no part at all.
 */
#include <stdint.h>
#include "regs.h"
#include "ss_board.h"
#include "ss_victim.h"

/* HFOSC is both the reset source and the part's maximum, so the clock is
 * not a build choice here the way it is on the L152RE. It is stated rather
 * than inherited so that a disagreement with the UART divisors below would
 * be a compile error rather than a wrong baud. */
#ifndef SS_CLK_MHZ
#define SS_CLK_MHZ 26
#endif
#if SS_CLK_MHZ != 26
#  error "ADuCM3029 runs at 26 MHz HFOSC: it is the reset source and the maximum"
#endif

#ifndef SS_BAUD
#define SS_BAUD 115200u
#endif
/* The four divisors in regs.h are a solved set for 115200 at a 26 MHz
 * PCLK, taken from ADI's own driver. Computing another rate would mean
 * trusting a divider model nothing here has checked against silicon, and a
 * baud that is a few percent off does not fail cleanly, it drops the
 * leading edge of characters. Refuse instead. */
#if SS_BAUD != 115200u
#  error "only SS_BAUD=115200 has an established divisor set on this part"
#endif

#if defined(SS_LINK_HEADER)
#  error "aducm3029 offers no header link: see board.mk, BOARD_LINKS is vcp only"
#elif !defined(SS_LINK_VCP)
#  error "define SS_LINK_VCP"
#endif

const char *ssb_name(void) { return "aducm3029"; }

/* ---- the accelerator -------------------------------------------------
 * One bit, ICEN, and regs.h says why there is nothing else. The write goes
 * to FLCC_CACHE_SETUP behind the unlock key; the READBACK comes from
 * FLCC_CACHE_STAT, which is a different register, and that is the whole
 * point: CACHE_KEY is consumed by the single write it precedes, so a
 * sequence that forgets to re-arm it leaves SETUP unchanged and silently
 * reports the configuration it asked for unless the readback is read.
 * Trap 1 in ss_board.h and the second rule in docs/PORTING.md.
 *
 * REFUSAL. On the L152RE "refuse what the clock cannot run" means refusing
 * zero wait states at 32 MHz. There is no equivalent here, because there
 * is no latency field to under-program and the part has one clock which is
 * also its maximum, so no accelerator word on this part can stop the next
 * instruction fetch from being served. What CAN be wrong is a word with
 * bits this part does not implement, which would otherwise be accepted,
 * discarded by the hardware, and recorded in a capture under the name it
 * was asked for. Those are refused, and SS_V_SET_ACR replies with the
 * readback so the caller sees that it was.
 *
 * TRAP 2, the cache dance, DOES NOT APPLY HERE AND THE REASON IS NOT
 * "there is no cache". There is one, 4 kB, but it is instruction-only and
 * the rig header establishes no invalidate or flush bit anywhere in FLCC,
 * only the enable. So there is no reset-the-cache-while-disabled step
 * available to perform, and clearing ICEN is the entire operation ADI's
 * own driver performs. If an invalidate bit is found later, it belongs
 * here. Whether disabling the cache also drops its contents on this part
 * is NOT ESTABLISHED.
 */
#define ACR_IMPLEMENTED FLCC_CACHE_ICEN   /* bit 0 and nothing else */

void ssb_acr_set(uint32_t v)
{
    if (v & ~(uint32_t)ACR_IMPLEMENTED) return;

    /* Fenced either side: this decides how the very next instruction fetch
     * is served, which is the same reason the STM32 ports fence FLASH_ACR. */
    __asm volatile("dsb; isb");
    FLCC_CACHE_KEY = FLCC_CACHE_KEY_VAL;
    if (v & FLCC_CACHE_ICEN) FLCC_CACHE_SETUP |=  FLCC_CACHE_ICEN;
    else                     FLCC_CACHE_SETUP &= ~FLCC_CACHE_ICEN;
    __asm volatile("dsb; isb");
}

uint32_t ssb_acr_get(void)
{
    return FLCC_CACHE_STAT & FLCC_CACHE_ICEN;
}

uint32_t ssb_clk_hz(void) { return HFOSC_HZ; }

/* One clock, and it is the reset source and the maximum. Reported as
 * unavailable rather than faked, which is what the contract asks for. */
int ssb_set_mhz(uint32_t mhz) { (void)mhz; return 0; }

/* No hit/miss counters: the rig header's FLCC list has a cache enable and
 * a cache status bit and nothing that counts. */
int  ssb_has_monitors(void) { return 0; }
void ssb_monitors_reset(void) {}
void ssb_monitors_read(uint32_t *hit, uint32_t *miss) { *hit = 0u; *miss = 0u; }

/* ---- clock ------------------------------------------------------------
 * HFOSC 26 MHz is the reset source, so there is no PLL to bring up. Both
 * dividers are pinned at 1 so HCLK and PCLK are 26 MHz and stated rather
 * than inherited: the UART divisors are computed against PCLK, and a PCLK
 * left at whatever the last image set would change the baud without
 * changing anything visible here.
 *
 * READ-MODIFY-WRITE, NOT A PLAIN STORE. CLK_CTL1 also holds ACLKDIVCNT in
 * bits 23:16, and a whole-register write would drive that divider to ZERO
 * on the way past: a divide-by-zero on a clock tree that feeds the flash
 * controller, on a board that has no working recovery path here.
 */
static void clock_init(void)
{
    CLKG_CLK_CTL1 = (CLKG_CLK_CTL1 & ~0x0000FFFFu)
                  | (1u << 0)     /* HCLKDIVCNT = 1 */
                  | (1u << 8);    /* PCLKDIVCNT = 1 */
}

/* ---- the link ---------------------------------------------------------
 * UART0 on P0_10 (TX) and P0_11 (RX), which is where the EVAL-ADICUP3029's
 * DAPLink CDC port is. It is the only link this part offers in this tree,
 * so BOARD_LINKS names just `vcp`.
 */
static void uart_init(void)
{
    /* P0_10 / P0_11 to mux function 1, UART0_TX / UART0_RX. */
    GPIO0_CFG = (GPIO0_CFG & ~((3u << GPIO0_CFG_P0_10_Pos)
                             | (3u << GPIO0_CFG_P0_11_Pos)))
              | (1u << GPIO0_CFG_P0_10_Pos)
              | (1u << GPIO0_CFG_P0_11_Pos);

    UART0_IEN  = 0;
    UART0_LCR  = UART_LCR_WLS_8BIT;                  /* 8N1 */
    UART0_LCR2 = UART_OSR_115200;
    UART0_DIV  = UART_DIV_115200;
    UART0_FBR  = UART_FBR_FBEN
               | (UART_DIVM_115200 << UART_FBR_DIVM_Pos)
               | UART_DIVN_115200;
    /* FIFOs off. The measured region never touches the UART, and a FIFO
     * only adds a way for a reply to sit unflushed. */
    UART0_FCR  = 0;
    /* FORCECLK holds UCLK on rather than letting it gate when the UART is
     * idle. ADI's own driver sets it, and the failure it prevents is the
     * quiet kind: a gated clock costs the leading edge of a character, so
     * the symptom is dropped or corrupted first bytes on an otherwise
     * working link rather than silence. */
    UART0_CTL  = UART_CTL_FORCECLK;
}

void ssb_put(uint8_t c)
{
    while (!(UART0_LSR & UART_LSR_THRE)) {}
    UART0_TX = c;
}

int ssb_get(void)
{
    while (!(UART0_LSR & UART_LSR_DR)) {}
    return (int)(UART0_RX & 0xFFu);
}

/* ---- trigger ----------------------------------------------------------
 * THERE IS NO TRIGGER PIN ON THIS PORT, AND THESE ARE NOT PLACEHOLDERS TO
 * BE FILLED IN FROM A DATASHEET BY GUESSWORK.
 *
 * Driving one needs the GPIO port's output-enable and set/clear data
 * registers. rigs/fw_aducm_aes/ drives no GPIO output, so its header
 * carries only GPIO0_CFG, the mux register, and the offsets of the rest
 * are not established by anything this port has read. A wrong peripheral
 * offset on this part does not fault: at best the marker never appears, at
 * worst the write lands in another field of the same block and changes
 * something in the measured path. So these stay empty, and an externally
 * instrumented capture is not available here until the output registers
 * are established from ADI's own header and a board is attached to check
 * them against.
 *
 * SS_V_IDENT WILL STILL SET SS_VI_HAS_TRIGGER, because ss_victim.c sets
 * that flag unconditionally for every part and a port is not allowed to
 * edit it. A host reading this port's IDENT should not believe the flag.
 * The self-timed path (SS_V_ENC_CYC, SysTick) is unaffected and is how the
 * research rig measured this part.
 */
void ssb_trig_high(void) {}
void ssb_trig_low(void)  {}

/* ---- the optional cycle counter --------------------------------------
 * SysTick is a 24-bit DOWN counter at the core clock. At 26 MHz it wraps
 * every 0.645 s, far longer than one encryption.
 *
 * THE BOARD OWNS THE SUBTRACTION, trap 3 in ss_board.h: a caller doing
 * `second - first` on a down-counter gets large positive garbage rather
 * than an error. The mask is 24 bits because that is SysTick's width; a
 * DWT CYCCNT would be 32 and unmasked, which is the other half of why this
 * hook exists.
 */
#if defined(SS_TIMING_DWT)
#  error "no DWT on this port: the rig header establishes no DWT registers for \
this part and CYCCNT is optional on a Cortex-M3, so an unimplemented counter \
would read zero for every window and look like constant time. Use TIMING=systick."
#endif

#ifdef SS_TIMING_SYSTICK
int      ssb_has_cyc(void)  { return 1; }
uint32_t ssb_cyc_read(void) { return SYST_CVR; }
uint32_t ssb_cyc_delta(uint32_t first, uint32_t second)
{
    return (first - second) & 0xFFFFFFu;   /* counts DOWN, 24 bits */
}
#else
/* The arm with no instrumentation in it. SS_V_ENC_CYC answers with the
 * error response rather than with zeros, so the host learns it from the
 * device instead of reading a column of zeros as a constant-time result. */
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
    uart_init();
    /* Boot with the instruction cache ON, which is what ADI's own
     * system_ADuCM3029.c leaves it as and therefore what a product ships.
     * A sweep that wants it off asks for it through SS_V_SET_ACR and gets
     * the readback. */
    ssb_acr_set(FLCC_CACHE_ICEN);
#ifdef SS_TIMING_SYSTICK
    SYST_RVR = 0xFFFFFFu;
    SYST_CVR = 0;
    SYST_CSR = 5u;     /* ENABLE | CLKSOURCE = core clock, no interrupt */
#endif
}
