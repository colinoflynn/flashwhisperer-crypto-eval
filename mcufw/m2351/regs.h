/* Nuvoton M2351 (M2351KIAAE, NuMaker-PFM-M2351) registers, and the
 * accelerator-word packing.
 *
 * Every address and bit position below was copied from the rig that measured
 * this part on silicon, rigs/fw_m2351_aes/main.c in the flash-accelerator-tests
 * tree, which took them from the Nuvoton BSP headers (fmc_reg.h, clk_reg.h,
 * sys_reg.h) and then exercised them on the die. Nothing here was inferred
 * from a family datasheet, and a register this port would like to have but
 * that the rig does not contain is ABSENT rather than guessed. That is why
 * there is no GPIO block and no UART block in this file: see the trigger and
 * console notes in board.c.
 *
 * THESE ARE THE SECURE ALIASES. The part boots Secure with the SAU disabled
 * and this victim, like the rig, is a single secure image that never
 * configures the SAU or the SCU, so peripherals answer at 0x4xxx_xxxx, flash
 * is addressed at 0x0000_0000 and SRAM at 0x2000_0000. The non-secure views
 * (0x1xxx_xxxx flash, 0x3xxx_xxxx SRAM) are not used here. On this part they
 * are not even aliases of the same storage: research/targets/m2351.md section
 * 5b measured the flash split to be a PARTITION, each physical address
 * answering at exactly one of the two.
 *
 * ---- packing of the opaque accelerator word -----------------------------
 *
 *     bits 0..3   FMC_CYCCTL.CYCLE   flash access cycles, 1..3 usable
 *     bit  8      FMC_CYCCTL.FADIS   0 = hardware auto-tunes CYCLE
 *                                    1 = software owns it
 *
 * UNLIKE THE H723 AND THE RA2A1, NOTHING IS SYNTHESISED HERE. The whole flash
 * read path on this part is one register, so the word is that register masked
 * to its two defined fields and the bit positions are the silicon's own. The
 * mask exists so that a reserved bit reading back nonzero cannot make an
 * accepted configuration look refused: tools/capture.py compares the readback
 * with the request and captures nothing when they differ.
 *
 * What is NOT in the word, and why:
 *
 *   There is no cache-enable bit, no prefetch bit and no invalidate bit to
 *   put there. The cache's existence is attested only by the words "if cache
 *   miss" in the CYCLE field description; no register in the map names it.
 *   The geometry was measured anyway - 4 KB, direct-mapped, 8-byte lines,
 *   indexed on A[11:3] - see research/reference/DATASHEETS.md.
 *
 *   There is no FTCTL. The register other NuMicro families put at FMC+0x18,
 *   carrying the FOM frequency-optimisation field, is reserved on this part.
 *
 *   FMC_ISPSTS.FCYCDIS is read back in ssb_acr_set but is deliberately not a
 *   bit of the word. It is read-only, so a host could not request it, and a
 *   readback that carried a bit the request could not carry would read as a
 *   refusal on every successful write.
 */
#ifndef M2351_REGS_H
#define M2351_REGS_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define DSB()  __asm volatile("dsb" ::: "memory")
#define ISB()  __asm volatile("isb" ::: "memory")

static inline void irq_off(void) { __asm volatile("cpsid i" ::: "memory"); }
static inline void irq_on(void)  { __asm volatile("cpsie i" ::: "memory"); }

/* ---- the accelerator word ---------------------------------------------- */
#define M2351_ACR_CYCLE_Msk   0x0000000Fu
#define M2351_ACR_FADIS       0x00000100u
#define M2351_ACR_Msk         (M2351_ACR_CYCLE_Msk | M2351_ACR_FADIS)

/* ---- SYS (0x40000000) ---------------------------------------------------
 * Only the register lock. SYS_REGLCTL guards FMC_CYCCTL and the clock
 * registers both: a write to either without the unlock sequence in front of
 * it is SILENTLY IGNORED rather than faulted, which is how a configuration
 * axis quietly stops applying and a part gets written up as flat. */
#define SYS_REGLCTL  REG(0x40000100u)
#define REGLCTL_UNLOCKED (1u << 0)           /* reads 1 while unlocked */

/* ---- CLK (0x40000200) --------------------------------------------------- */
#define CLK_PWRCTL   REG(0x40000200u)
#define CLK_CLKSEL0  REG(0x40000210u)
#define CLK_CLKDIV0  REG(0x40000220u)
#define CLK_STATUS   REG(0x40000250u)

#define PWRCTL_HIRCEN      (1u << 2)         /* the 12 MHz internal RC   */
#define STATUS_HIRCSTB     (1u << 4)
#define CLKSEL0_HCLKSEL_Msk 0x7u
#define HCLKSEL_HIRC       7u
#define CLKDIV0_HCLKDIV_Msk 0xFu

/* ---- FMC (0x4000C000) ---------------------------------------------------
 * The whole flash read path, and nothing else from this peripheral is used.
 * FCYCDIS is the read-only mirror of FADIS, in a different register from the
 * one FADIS is written to, which is what lets a silently-ignored protected
 * write be told from a successful one. */
#define FMC_ISPSTS   REG(0x4000C040u)
#define FMC_CYCCTL   REG(0x4000C04Cu)
#define ISPSTS_FCYCDIS     (1u << 4)

/* ---- SysTick (ARMv8-M Baseline core) ------------------------------------
 * The time base on this part. There is no DWT here at all - board.c has the
 * measurement that says so. */
#define SYST_CSR     REG(0xE000E010u)
#define SYST_RVR     REG(0xE000E014u)
#define SYST_CVR     REG(0xE000E018u)
#define SYST_MASK    0x00FFFFFFu             /* 24 bits, counting DOWN */
#define SYST_ENABLE_CORECLK 5u               /* ENABLE | CLKSOURCE = HCLK */

#endif /* M2351_REGS_H */
