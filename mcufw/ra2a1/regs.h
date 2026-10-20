/* Renesas RA2A1 (R7FA2A1AB3CFM) registers, and the accelerator-word packing.
 *
 * Every address and bit position below was copied from the rig that measured
 * this part, rigs/fw_ra2_aes/main.c in the flash-accelerator-tests tree, which
 * took them from the Renesas FSP device header R7FA2A1AB.h and then exercised
 * them on the die. Nothing here was inferred from a family datasheet. Where a
 * register this port would like to have is not in that rig it is absent from
 * this file rather than guessed, which is why there is no GPIO and no SCI
 * block here: see the trigger and console notes in board.c.
 *
 * THE ACCELERATOR ON THIS PART IS THREE REGISTERS IN TWO PERIPHERALS, so the
 * one opaque word ss_board.h asks for is PACKED, the escape hatch that
 * contract documents for exactly this case. There is no FLASH_ACR analogue:
 * the flash controller has no wait-state field that the clock code uses, and
 * the cache enable is not in the same peripheral as the wait state the part
 * actually runs on.
 *
 *     bit  0       FCACHEE.FCACHEEN   0x4001C100 bit 0, flash cache enable
 *     bit  4       SYSTEM.MEMWAIT     0x4001E031 bit 0, 0 or 1 memory waits
 *     bits 8..10   FCACHE FLWT        0x4001C11C bits [2:0], flash wait cycles
 *
 * The three fields are spread one per nibble so that a readback is legible in
 * hex without a mask: 0x011 is the configuration the part ships in, 0x010 is
 * the same with the cache off, 0x711 is the most heavily stalled cell with the
 * cache still on. The host never interprets the word, it round-trips through
 * SS_V_SET_ACR into the capture metadata, and ssb_acr_get READS all three
 * registers back rather than reconstructing the request, which is what makes a
 * refused configuration visible instead of recorded under the name it was
 * asked for.
 *
 * Why all three fields are in the word rather than only the cache bit:
 *
 *   FCACHEEN is the channel. research/targets/ra2a1.md section 7 has it
 *   closing the channel bit-exactly, which is the first off switch on that
 *   bench that reaches the whole structure, so it is the one axis that must
 *   never be absent from a capture's metadata.
 *
 *   MEMWAIT is what prices a miss (2.000 cycles with the cache on) and it is
 *   CLOCK-COUPLED: 0 is prohibited while ICLK is above 32 MHz. That coupling
 *   is enforced in ssb_acr_set, not here.
 *
 *   FLWT looks like a dead field shared across the RA family and is not.
 *   ra2a1.md section 1a wrote all eight values and measured each step above 1
 *   adding about five cycles to every flash read, so it is a genuine third
 *   axis and belongs in the word. It reads 0 on this die at reset and nothing
 *   in startup writes it.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))

/* ---- packing of the opaque accelerator word ---------------------------- */
#define RA_ACR_FCACHEEN   0x00000001u
#define RA_ACR_MEMWAIT    0x00000010u
#define RA_ACR_FLWT_Msk   0x00000700u
#define RA_ACR_FLWT_Pos   8u

/* ---- SYSTEM (0x4001E000): clock generation and the memory wait --------- */
#define SYSTEM_BASE  0x4001E000u
#define SCKDIVCR     REG32(SYSTEM_BASE + 0x020u) /* PCKD[2:0] PCKB[10:8] ICK[26:24] FCK[30:28] */
#define SCKSCR       REG8 (SYSTEM_BASE + 0x026u) /* CKSEL[2:0]; 0 = HOCO                  */
#define MEMWAIT      REG8 (SYSTEM_BASE + 0x031u) /* [0] 0 or 1 waits; 0 banned above 32 MHz */
#define HOCOCR       REG8 (SYSTEM_BASE + 0x036u) /* [0] HCSTP: 0 = HOCO running           */
#define OSCSF        REG8 (SYSTEM_BASE + 0x03Cu) /* [0] HOCOSF: 1 = HOCO stable           */
#define OPCCR        REG8 (SYSTEM_BASE + 0x0A0u) /* [1:0] OPCM: 0 = high speed            */
#define PRCR         REG16(SYSTEM_BASE + 0x3FEu) /* 0xA5 key in [15:8]; bit 0 PRC0        */

#define OPCCR_OPCMTSF  (1u << 4)                 /* 1 while a mode change is in flight    */
#define PRCR_UNLOCK    0xA501u
#define PRCR_LOCK      0xA500u

/* The divider word the factory image was running when the part arrived, read
 * off the die before the first flash: ICLK/1, PCLKB/2, FCLK/2. Holding PCLKB
 * and FCLK at /2 is what keeps them in spec while ICLK moves, so this is the
 * literal the rig writes rather than a recomputed one. */
#define SCKDIVCR_ICLK1_PCLKB2_FCLK2  ((1u << 28) | (0u << 24) | (1u << 8) | 1u)

/* ---- FCACHE (0x4001C000): the flash cache and its own wait field ------- */
#define FCACHE_BASE  0x4001C000u
#define FCACHEE      REG16(FCACHE_BASE + 0x100u) /* [0] FCACHEEN                          */
#define FCACHEIV     REG16(FCACHE_BASE + 0x104u) /* [0] FCACHEIV, self-clearing           */
#define FLWT         REG8 (FCACHE_BASE + 0x11Cu) /* [2:0] flash wait cycles, 0..7         */

/* ---- SysTick, the only time base this core has ------------------------- */
#define SYST_CSR     REG32(0xE000E010u)
#define SYST_RVR     REG32(0xE000E014u)
#define SYST_CVR     REG32(0xE000E018u)
#define SYST_MASK    0x00FFFFFFu                 /* 24 bits, counting DOWN               */
#define SYST_ENABLE_CORECLK  5u                  /* ENABLE | CLKSOURCE = processor clock */

#define DSB() __asm volatile("dsb 0xF" ::: "memory")
#define ISB() __asm volatile("isb 0xF" ::: "memory")

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

#endif /* SS_REGS_H */
