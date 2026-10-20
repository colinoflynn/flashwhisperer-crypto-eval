/* Renesas Synergy S5D3 (R7FS5D37A3A01CFP) registers, by hand, and the
 * accelerator-word packing.
 *
 * EVERY S5D3 ADDRESS AND BIT POSITION BELOW IS COPIED FROM
 * rigs/fw_s5d3_aes/main.c IN THE RESEARCH TREE, where they were read off the
 * running factory image on the die. Nothing here is inferred from a family
 * datasheet or from the RA2A1 port beside it, because three facts that look
 * like Renesas facts are RA2A1 facts and are inverted on this part. The four
 * core registers at the bottom are the exception and say so.
 *
 * ---- THE ACCELERATOR WORD -------------------------------------------------
 *
 * The contract (instrumentation/victim/include/ss_board.h) wants ONE opaque
 * word. This part does not have one register that holds the accelerator, so
 * the word is PACKED here, the same escape hatch mcufw/stm32h723zg/regs.h
 * uses for its L1 caches. The two halves live in the same peripheral but in
 * different registers and at different widths:
 *
 *     bits 0..2   FLWT[2:0]      flash wait cycles, 8-bit reg at 0x4001C11C
 *     bit  8      FCACHEEN       flash cache enable, 16-bit reg at 0x4001C100
 *
 * Nothing else is in the word, and two omissions are deliberate.
 *
 * MEMWAIT (0x4001E031) is NOT an axis on this part. On the RA2A1 it is the
 * primary wait-state control and FLWT is the secondary one that was nearly
 * written off; here it is the reverse. The rig records MEMWAIT reading 0 on
 * this die with no effect it could find, and FLWT as the only wait-state
 * knob that does anything. It is defined below for reporting and is never
 * written.
 *
 * FCACHEIV (0x4001C104) is a self-clearing invalidate pulse, not a state, so
 * it has nothing to round-trip through the word. It is used by the set path
 * and never read back as configuration.
 *
 * The host never interprets the word. It round-trips through SS_V_SET_ACR and
 * lands in the capture's metadata, and ssb_acr_get REASSEMBLES it by reading
 * the two registers, never by remembering what was requested. That is what
 * makes a refused configuration visible instead of being recorded under the
 * name it was asked for.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- packing of the opaque accelerator word --------------------------- */
#define S5D3_ACR_FLWT_Msk   0x00000007u
#define S5D3_ACR_FCACHEEN   0x00000100u

/* ---- flash cache and wait states -------------------------------------- */
#define FCACHE_BASE_REG   0x4001C000u
#define FCACHEE           (*(volatile uint16_t *)(FCACHE_BASE_REG + 0x100u))
#define FCACHEE_FCACHEEN  (1u << 0)
#define FCACHEIV          (*(volatile uint16_t *)(FCACHE_BASE_REG + 0x104u))
#define FCACHEIV_FCACHEIV (1u << 0)
#define FLWT              (*(volatile uint8_t  *)(FCACHE_BASE_REG + 0x11Cu))
#define FLWT_Msk          0x07u

/* ---- clock generation circuit ----------------------------------------- */
#define SYSTEM_BASE_REG   0x4001E000u
/* PCKD[3:0] PCKC[7:4] PCKB[11:8] PCKA[15:12] BCK[19:16] ICK[27:24] FCK[31:28] */
#define SCKDIVCR          (*(volatile uint32_t *)(SYSTEM_BASE_REG + 0x020u))
#define SCKSCR            (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x026u))
#define SCKSCR_CKSEL_PLL  5u
#define PLLCCR            (*(volatile uint16_t *)(SYSTEM_BASE_REG + 0x028u))
#define PLLCR             (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x02Au))
/* Reads 0 on this die and has no effect the rig can find. Reported, never
 * written. The wait-state axis on this part is FLWT. */
#define MEMWAIT           (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x031u))
#define MOSCCR            (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x032u))
#define OSCSF             (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x03Cu))
#define OSCSF_MOSCSF      (1u << 3)
#define OSCSF_PLLSF       (1u << 5)
#define OPCCR             (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x0A0u))
#define OPCCR_TSF         (1u << 4)
#define MOSCWTCR          (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x0A2u))
#define MOMCR             (*(volatile uint8_t  *)(SYSTEM_BASE_REG + 0x413u))
/* Register write protection. PRC0 guards the clock generation circuit; the
 * rig unlocks PRC0, PRC1 and PRC3 together around its FLWT and divider
 * writes and locks them again after, so this port does the same. */
#define PRCR              (*(volatile uint16_t *)(SYSTEM_BASE_REG + 0x3FEu))
#define PRCR_UNLOCK       0xA50Bu
#define PRCR_LOCK         0xA500u

/* ---- core ---------------------------------------------------------------
 * CPUID, DEMCR, DWT_CTRL and DWT_CYCCNT are in the rig, which reads
 * DWT_CTRL = 0x40000001 on this die: bit 25 NOCYCCNT is CLEAR, so CYCCNT is
 * implemented and is a 32-bit up-counter. The RA2A1 beside it has no CYCCNT
 * at all, which is the first of the three inversions.
 *
 * SCB_VTOR and the three SysTick registers are NOT in the rig, which drives
 * the part over SWD and uses neither. They are ARMv7-M architectural
 * addresses and are taken from mcufw/stm32f401re/regs.h, which runs on
 * silicon in this tree. That is the only place in this header where the
 * source is not the S5D3 rig.
 */
#define CPUID_REG         (*(volatile uint32_t *)0xE000ED00u)
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08u)

#define SYST_CSR          (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR          (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR          (*(volatile uint32_t *)0xE000E018u)

#define DEMCR             (*(volatile uint32_t *)0xE000EDFCu)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL          (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT        (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
