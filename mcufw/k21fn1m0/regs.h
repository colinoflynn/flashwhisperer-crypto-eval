/* NXP Kinetis MK21FN1M0VMC12 registers, by hand, and the accelerator-word
 * packing.
 *
 * EVERY ADDRESS AND EVERY BIT POSITION BELOW IS COPIED FROM A SOURCE, and
 * the source is named beside it. Most come from
 * rigs/fw_k21_aes/main.c in the flash-accelerator-tests tree, which is the
 * firmware that was actually run on this silicon. Two read-only fields
 * (B0RWSC, B0MW) are not decoded in that file because its host does the
 * decoding; those two come from the host half of the same rig,
 * results/k21fn1m0/k21.py, and are marked. Nothing here is inferred from a
 * datasheet this port has not got in front of it.
 *
 * ---- THE ACCELERATOR ON THIS PART IS THREE REGISTERS ---------------------
 *
 * ss_board.h asks for ONE opaque word and this part does not have one, so
 * the word is packed here, which is the escape hatch that contract
 * documents. Three things have to fit in it:
 *
 *   FMC_PFB0CR   the five separate enables, the replacement policy and the
 *                way lock for flash block 0 (0x00000..0x7FFFF)
 *   FMC_PFB1CR   the same fields for flash block 1 (0x80000..0xFFFFF)
 *   SIM_CLKDIV1  OUTDIV4, the flash clock divider, which is the ONLY way to
 *                move the wait state on this part - B0RWSC is read-only and
 *                the FMC derives it from the system:flash clock ratio
 *
 * The packing:
 *
 *     bits  4..0   the five FMC enables, at their own register positions:
 *                    0 B0SEBE  single entry buffer
 *                    1 B0IPE   instruction prefetch
 *                    2 B0DPE   data prefetch
 *                    3 B0ICE   instruction cache
 *                    4 B0DCE   data cache
 *     bits  7..5   CRC, cache replacement policy, also at its own position
 *     bits 11..8   CLCK_WAY, way lock (PFB0CR bits 27..24, moved down)
 *     bits 15..12  OUTDIV4 as the RAW FIELD, i.e. divisor - 1
 *     bits 19..16  B0RWSC, READ-ONLY, appears in the readback only
 *     bits 21..20  B0MW, READ-ONLY, appears in the readback only
 *     bits 31..22  reserved. ssb_acr_set REFUSES a word with any of these.
 *
 * So the low byte of the packed word is literally the low byte of
 * FMC_PFB0CR, and 0x101F - all five enables on, LRU, no lock, OUTDIV4 = 2 -
 * is the state this part comes out of reset in.
 *
 * BOTH BANKS GET THE SAME WRITABLE FIELDS. The rig writes both for one
 * reason, and it is worth repeating here because the symptom is invisible:
 * a configuration applied to one bank while the tables live in the other
 * controls nothing, reads flat, and is a false null with no error. The two
 * banks have genuinely independent caches, speculation buffers and
 * single-entry buffers, so there is no spill-over to save you.
 *
 * THE READ-ONLY FIELDS ARE IN THE READBACK AND NOWHERE ELSE. ssb_acr_get
 * reassembles the word from FMC_PFB0CR and SIM_CLKDIV1 as they actually
 * read, never from what was requested, which is what makes a refused or
 * partially-applied configuration visible. B0RWSC in particular is the
 * confirmation this part offers and the ST parts cannot: it says what wait
 * state the FMC decided on for the clock ratio that was programmed.
 * ssb_acr_set ignores bits 21..16 on the way in rather than refusing them,
 * so a host that echoes a readback back at the device still works.
 *
 * WHAT IS DELIBERATELY NOT IN THE WORD:
 *
 *   CINV_WAY (PFB0CR 23..20) and S_B_INV (bit 19) are write-only pulses
 *   that read back as zero, not state. They are not an axis; they are part
 *   of the apply sequence and ssb_acr_set issues them on every write.
 *
 *   OUTDIV1, the core clock divider in SIM_CLKDIV1 bits 31..28. Changing it
 *   would change what ssb_clk_hz() reports and every cycle count with it,
 *   which is a different experiment from moving the accelerator. This port
 *   leaves it alone; the wait-state axis is reached by dividing the flash
 *   clock down instead, which is exactly what the rig's cmd 10 does.
 */
#ifndef SS_REGS_H
#define SS_REGS_H

#include <stdint.h>

/* ---- packing of the opaque accelerator word --------------------------- */
#define K21_ACR_ENABLES_Msk   0x0000001Fu
#define K21_ACR_CRC_Msk       0x000000E0u
#define K21_ACR_LOWBYTE_Msk   0x000000FFu   /* enables + CRC, in register order */
#define K21_ACR_CLCK_WAY_Pos  8u
#define K21_ACR_CLCK_WAY_Msk  0x00000F00u
#define K21_ACR_OUTDIV4_Pos   12u
#define K21_ACR_OUTDIV4_Msk   0x0000F000u
#define K21_ACR_B0RWSC_Pos    16u
#define K21_ACR_B0RWSC_Msk    0x000F0000u
#define K21_ACR_B0MW_Pos      20u
#define K21_ACR_B0MW_Msk      0x00300000u
/* Read-only in the readback, accepted and ignored on a set. */
#define K21_ACR_RDONLY_Msk    (K21_ACR_B0RWSC_Msk | K21_ACR_B0MW_Msk)
/* Nothing is defined up here. A set that touches it is refused. */
#define K21_ACR_RESERVED_Msk  0xFFC00000u

/* The state the part is in out of reset, measured on silicon and recorded
 * in research/targets/k21fn1m0.md section 1c: PFB0CR = PFB1CR = 0x1004001F
 * (all five enables, CRC = 0, B0MW = 0b10, B0RWSC = 1) with
 * SIM_CLKDIV1 = 0x00110000 (OUTDIV1 /1, OUTDIV4 /2). Packed, that is: */
#define K21_ACR_RESET         0x0000101Fu

/* ---- FMC: rigs/fw_k21_aes/main.c, the "FMC" block --------------------- */
#define FMC_BASE_REG      0x4001F000u
#define FMC_PFAPR         (*(volatile uint32_t *)(FMC_BASE_REG + 0x00u))
#define FMC_PFB0CR        (*(volatile uint32_t *)(FMC_BASE_REG + 0x04u))
#define FMC_PFB1CR        (*(volatile uint32_t *)(FMC_BASE_REG + 0x08u))

#define FMC_PFB_B0SEBE    (1u << 0)
#define FMC_PFB_B0IPE     (1u << 1)
#define FMC_PFB_B0DPE     (1u << 2)
#define FMC_PFB_B0ICE     (1u << 3)
#define FMC_PFB_B0DCE     (1u << 4)
#define FMC_PFB_CRC_Pos   5u
#define FMC_PFB_CRC_Msk   0x000000E0u
#define FMC_PFB_S_B_INV   (1u << 19)
#define FMC_PFB_CINV_WAY_Pos 20u
#define FMC_PFB_CLCK_WAY_Pos 24u

/* These two are read-only and the rig's firmware never decodes them, so the
 * positions come from the host half of the same rig,
 * results/k21fn1m0/k21.py: b0rwsc=(pfb0 >> 28) & 0xF and
 * b0mw={0:32,1:64,2:128}[(pfb0 >> 17) & 3]. */
#define FMC_PFB_B0MW_Pos     17u
#define FMC_PFB_B0MW_Msk     0x00060000u
#define FMC_PFB_B0RWSC_Pos   28u
#define FMC_PFB_B0RWSC_Msk   0xF0000000u

/* ---- SIM: rigs/fw_k21_aes/main.c, the "SIM" block --------------------- */
#define SIM_SDID          (*(volatile uint32_t *)0x40048024u)
#define SIM_CLKDIV1       (*(volatile uint32_t *)0x40048044u)
#define SIM_FCFG1         (*(volatile uint32_t *)0x4004804Cu)
#define SIM_FCFG2         (*(volatile uint32_t *)0x40048050u)
/* OUTDIV fields hold (divisor - 1): OUTDIV1 in 31..28, OUTDIV4 in 19..16.
 * rigs/fw_k21_aes/main.c, set_clkdiv(). */
#define SIM_CLKDIV1_OUTDIV1_Pos 28u
#define SIM_CLKDIV1_OUTDIV4_Pos 16u
#define SIM_CLKDIV1_OUTDIV4_Msk 0x000F0000u

/* ---- watchdog: rigs/fw_k21_aes/startup.c ------------------------------ */
#define WDOG_STCTRLH      (*(volatile uint16_t *)0x40052000u)
#define WDOG_UNLOCK       (*(volatile uint16_t *)0x4005200Eu)
#define WDOG_UNLOCK_KEY1  0xC520u
#define WDOG_UNLOCK_KEY2  0xD928u
/* STCTRLH resets to 0x01D3 with WDOGEN in bit 0 set and ALLOWUPDATE in bit 4
 * set; 0x01D2 is that same value with WDOGEN cleared. */
#define WDOG_STCTRLH_OFF  0x01D2u

/* ---- core ------------------------------------------------------------- */
/* DWT_CTRL, DWT_CYCCNT and DEMCR are used at these addresses by
 * rigs/fw_k21_aes/main.c. SCB_VTOR and the SysTick block are not touched by
 * that rig; they are ARMv7-M architectural addresses and are taken from the
 * sibling ports in this tree (mcufw/stm32f401re/regs.h), which run on the
 * same core generation. */
#define SCB_VTOR          (*(volatile uint32_t *)0xE000ED08u)

#define SYST_CSR          (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR          (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR          (*(volatile uint32_t *)0xE000E018u)

#define DEMCR             (*(volatile uint32_t *)0xE000EDFCu)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL          (*(volatile uint32_t *)0xE0001000u)
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CTRL_NOCYCCNT  (1u << 25)
#define DWT_CYCCNT        (*(volatile uint32_t *)0xE0001004u)

#endif /* SS_REGS_H */
