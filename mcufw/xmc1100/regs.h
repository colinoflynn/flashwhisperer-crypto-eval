/* Infineon XMC1100-Q024F0064 (Cortex-M0) registers, by hand.
 *
 * EVERY ADDRESS AND BIT POSITION IN THIS FILE WAS COPIED FROM
 * rigs/fw_xmc1_aes/ IN THE RESEARCH TREE, which is firmware that has run on
 * this die. Nothing here was taken from memory or from a datasheet this
 * port did not read. Where that rig does not establish a register, this
 * file does not define one, and board.c says what it could not do as a
 * result - the console and the trigger pin are both in that position.
 *
 * THERE IS NO FLASH ACCELERATOR ON THIS PART. No cache, no prefetch buffer,
 * no read buffer, and no read latch either: the reference manual's flash
 * chapter never uses any of those words, and research/targets/xmc1100.md
 * measured the absence rather than inferring it - reading one byte twice
 * costs exactly what reading two rows 4 KB apart costs, to the cycle. So
 * the one opaque word ss_board.h asks for is NOT an accelerator word here.
 *
 * What it carries instead is the whole of the flash READ PATH that software
 * can reach on this part, which is three fields in two registers:
 *
 *     bit  0      NVMCONF.WS        0 or 1 fixed wait states
 *     bit  1      CONFIG1.FIXWS     0 adaptive (as shipped), 1 fixed
 *     bits 4..5   NVMCONF.HRLEV     hard read level, the read margin
 *
 * Every other bit of the word is reserved and board.c REFUSES a value that
 * sets one, so a word meant for some other part cannot be written here and
 * then recorded as if it had been.
 *
 * Why those three and nothing else. FIXWS is the only bit on the part that
 * changes timing BEHAVIOUR rather than timing cost: in the adaptive scheme
 * the controller resolves differently from run to run above roughly 21 MHz,
 * and the shipped configuration is therefore the noisy one. WS is the cost
 * knob the scheme selects between. HRLEV is in the same register as WS and
 * is a large lever - it costs 64 % on a whole encryption - which is why it
 * is here rather than left out: a reader who does not know the field exists
 * will mis-attribute the slowdown if anything ever sets it. All three are
 * measured as gain factors and not as information, which is the same answer
 * wait states got on the ST parts.
 *
 * The clock is not in the word, deliberately, although it gates the wait
 * state. The contract gives the clock its own hook, and putting MCLK in two
 * places is how a board file and a capture come to disagree.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* ---- packing of the opaque word, as documented above ------------------- */
#define XMC_ACR_WS          0x00000001u
#define XMC_ACR_FIXWS       0x00000002u
#define XMC_ACR_HRLEV_Pos   4u
#define XMC_ACR_HRLEV_Msk   0x00000030u
#define XMC_ACR_KNOWN_Msk   (XMC_ACR_WS | XMC_ACR_FIXWS | XMC_ACR_HRLEV_Msk)

/* ---- NVM, the flash controller (0x40050000) ---------------------------- */
#define NVM_NVMSTATUS   REG(0x40050000u)
#define NVM_NVMCONF     REG(0x40050008u)   /* [12] WS, [15] NVM_ON, [1:0] HRLEV */
#define NVM_CONFIG1     REG(0x40050048u)   /* [11] FIXWS                        */

#define NVMCONF_WS      (1u << 12)
#define NVMCONF_HRLEV   (3u << 0)
#define CONFIG1_FIXWS   (1u << 11)

/* ---- SCU (0x40010000) -------------------------------------------------- */
/* PASSWD unlocks the protected bits of CLKCR: 0xC0 to open, 0xC3 to close. */
#define SCU_IDCHIP      REG(0x40010004u)
#define SCU_PASSWD      REG(0x40010024u)

/* CLKCR is FDIV in [7:0] and IDIV in [15:8], and MCLK = 64 MHz / (2 * IDIV)
 * with FDIV refining the divisor by FDIV/256. The 64 MHz DCO behind that is
 * measured and not assumed: the rig host-timed 31.977 / 15.996 / 8.010 MHz
 * over 8e6 SysTick ticks for IDIV 1 / 2 / 4. VDDC2LOW reports that the core
 * supply has not yet caught up with a divider change. */
#define SCU_CLKCR       REG(0x40010300u)
#define CLKCR_FDIV_Msk  0x000000FFu
#define CLKCR_IDIV_Msk  0x0000FF00u
#define CLKCR_IDIV_Pos  8u
#define CLKCR_VDDC2LOW  (1u << 30)
#define XMC_DCO_HZ      64000000u

/* ---- Cortex-M0 core (ARMv6-M) ------------------------------------------
 * SysTick is the whole time base on this part. There is no DWT cycle
 * counter: ARMv6-M does not require one, Cortex-M0 does not implement one,
 * and on this die the NOCYCCNT bit reads CLEAR, which is the encoding for
 * "the counter is there". The rig wrote TRCENA and CYCCNTENA and read
 * DWT_CYCCNT twice around a spin, and the delta was 0. DWT is therefore not
 * defined in this file at all, so that no arm of this port can read it by
 * accident and record a column of zeros as a constant-time result.
 *
 * There is no VTOR either, which is why startup.c does not write one. */
#define SYST_CSR   REG(0xE000E010u)    /* [0] ENABLE, [2] CLKSOURCE = MCLK   */
#define SYST_RVR   REG(0xE000E014u)
#define SYST_CVR   REG(0xE000E018u)    /* 24-bit, counts DOWN                */
#define SYST_MASK  0x00FFFFFFu

#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_CLKSOURCE (1u << 2)   /* 1 = MCLK, which is what the rig uses */

#define CPUID_REG  REG(0xE000ED00u)

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

#endif /* SS_REGS_H */
